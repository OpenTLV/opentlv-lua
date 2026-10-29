#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Exercise LuaRocks publication against a mocked server, never the real registry."""

from copy import deepcopy
import io
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qs, urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import luarocks_release as publication  # noqa: E402
from test_ci_gates import ENV, LOCK, CORE_RELEASE, records  # noqa: E402

KEY = 'k3yFixtureForLuaRocksUploads0123'
ROCKSPEC, ROCK = 'opentlv-0.3.0-1.rockspec', 'opentlv-0.3.0-1.src.rock'


class Response(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


class Server:
    """A luarocks.org stand-in holding one account's files and the root manifests."""

    def __init__(self):
        self.files, self.version, self.module, self.taken = {}, None, None, False
        self.calls, self.fail, self.manifests = [], None, ['root']

    def __call__(self, request, timeout=None):
        url = urlsplit(request.full_url)
        self.calls.append((request.get_method(), url.path))
        if self.fail:
            raise self.fail
        if url.path.startswith('/manifests/marekcingel/'):
            name = url.path.rsplit('/', 1)[1]
            if name not in self.files:
                raise HTTPError(request.full_url, 404, 'Not Found', {}, None)
            return Response(self.files[name])
        if re.fullmatch(r'/manifest-5\.[1-4]\.json', url.path):
            return Response(json.dumps({'repository': {'opentlv': {}} if self.taken else {}}).encode())
        prefix = f'/api/1/{KEY}/'
        assert url.path.startswith(prefix), url.path
        method = url.path[len(prefix):]
        if method == 'check_rockspec':
            self.assert_query(url, {'package': ['opentlv'], 'version': ['0.3.0-1']})
            return self.json({'module': self.module, 'version': self.version})
        name, data = self.multipart(request)
        if method == 'upload':
            assert name == ROCKSPEC
            self.files[name], self.version, self.module = data, {'id': 7}, {'id': 3}
            return self.json({'is_new': True, 'manifests': self.manifests, 'version': self.version})
        assert method == 'upload_rock/7' and name == ROCK, method
        self.files[name] = data
        return self.json({'module_url': 'https://luarocks.org/modules/marekcingel/opentlv'})

    @staticmethod
    def assert_query(url, expected):
        assert parse_qs(url.query) == expected, url.query

    @staticmethod
    def json(value):
        return Response(json.dumps(value).encode())

    @staticmethod
    def multipart(request):
        boundary = request.get_header('Content-type').split('boundary=', 1)[1].encode()
        part = request.data.split(b'--' + boundary)[1]
        head, data = part.split(b'\r\n\r\n', 1)
        return re.search(rb'filename="([^"]+)"', head)[1].decode(), data[:-2]


class LuaRocksPublication(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.directory = Path(temp.name)
        (self.directory / ROCKSPEC).write_bytes(b'rockspec bytes')
        (self.directory / ROCK).write_bytes(b'source rock bytes')
        self.plan = {'tag': 'v0.3.0', 'commit': 'a' * 40, 'version': '0.3.0-1', 'release_id': 9, 'run_id': '123',
                     'run_attempt': '1', 'rockspec': ROCKSPEC, 'rockspec_sha256': 'b' * 64,
                     'rock': ROCK, 'rock_sha256': 'c' * 64}
        self.server = Server()
        server = patch.object(publication, 'urlopen', self.server)
        server.start()
        self.addCleanup(server.stop)

    def publish(self):
        return publication.publish(self.plan, self.directory, publication.LuaRocks(KEY), delay=0)

    def posts(self):
        return [path for method, path in self.server.calls if method == 'POST']

    def test_new_version_uploads_exact_bytes_once(self):
        receipt = self.publish()
        self.assertEqual(receipt['status'], 'published')
        self.assertEqual(receipt['module_url'], 'https://luarocks.org/modules/marekcingel/opentlv/0.3.0-1')
        self.assertEqual(self.server.files, {ROCKSPEC: b'rockspec bytes', ROCK: b'source rock bytes'})
        self.assertEqual(self.posts(), [f'/api/1/{KEY}/upload', f'/api/1/{KEY}/upload_rock/7'])
        self.assertNotIn(KEY, json.dumps(receipt))

    def test_identical_published_version_is_not_uploaded_again(self):
        self.server.files = {ROCKSPEC: b'rockspec bytes', ROCK: b'source rock bytes'}
        self.server.version, self.server.module = {'id': 7}, {'id': 3}
        self.assertEqual(self.publish()['status'], 'already-published')
        self.assertEqual(self.posts(), [])

    def test_interrupted_upload_adds_only_the_missing_source_rock(self):
        self.server.files = {ROCKSPEC: b'rockspec bytes'}
        self.server.version, self.server.module = {'id': 7}, {'id': 3}
        self.assertEqual(self.publish()['status'], 'published')
        self.assertEqual(self.posts(), [f'/api/1/{KEY}/upload_rock/7'])

    def test_existing_different_files_are_never_replaced(self):
        for files in ({ROCKSPEC: b'other rockspec'}, {ROCKSPEC: b'rockspec bytes', ROCK: b'other rock'}):
            self.server.files, self.server.version, self.server.calls = dict(files), {'id': 7}, []
            with self.subTest(files=list(files)), self.assertRaisesRegex(ValueError, 'immutable'):
                self.publish()
            self.assertEqual(self.posts(), [])

    def test_name_owned_elsewhere_or_left_out_of_root_manifest_fails(self):
        self.server.taken = True
        with self.assertRaisesRegex(ValueError, 'Another account owns'):
            self.publish()
        self.assertEqual(self.posts(), [])
        self.server.taken, self.server.manifests = False, []
        with self.assertRaisesRegex(ValueError, 'root manifest'):
            self.publish()
        self.assertEqual(self.posts(), [f'/api/1/{KEY}/upload'])

    def test_failures_never_reveal_the_key(self):
        failures = [HTTPError(f'https://luarocks.org/api/1/{KEY}/check_rockspec', 403, 'Forbidden', {}, None),
                    URLError(f'https://luarocks.org/api/1/{KEY}/check_rockspec')]
        for failure in failures:
            self.server.fail = failure
            with self.subTest(failure=type(failure).__name__), self.assertRaises(ValueError) as caught:
                self.publish()
            self.assertNotIn(KEY, str(caught.exception))
            self.assertIsNone(caught.exception.__cause__)
        self.server.fail = None
        # A server error may quote the request, including the key in its URL path.
        echoed = json.dumps({'errors': [f'Invalid key {KEY}', f'/api/1/{KEY}/check_rockspec']}).encode()
        with patch.object(Server, 'json', staticmethod(lambda value: Response(echoed))):
            with self.assertRaisesRegex(ValueError, r'^LuaRocks check_rockspec rejected the request$') as caught:
                self.publish()
            self.assertNotIn(KEY, str(caught.exception))
            self.assertIsNone(caught.exception.__cause__)
        for key in ('', 'short', 'has/slash/and-is-long-enough', 'x' * 15):
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'LUAROCKS_API_KEY'):
                publication.LuaRocks(key)

    def test_unserved_upload_fails(self):
        with patch.object(publication, 'published', lambda name: None), \
                self.assertRaisesRegex(ValueError, 'does not serve'):
            publication.publish(self.plan, self.directory, publication.LuaRocks(KEY), attempts=2, delay=0)


class ReleaseEvidence(unittest.TestCase):
    def manifest(self):
        with patch.dict(os.environ, ENV):
            request = dict(repository=ENV['GITHUB_REPOSITORY'], commit=ENV['GITHUB_SHA'], run_id=ENV['GITHUB_RUN_ID'],
                           run_attempt=ENV['GITHUB_RUN_ATTEMPT'], tag='v0.2.0', version='0.2.0',
                           lua_sha=ENV['GITHUB_SHA'], workflow_sha=ENV['GITHUB_SHA'], core_lock=LOCK,
                           core_release=CORE_RELEASE)
            quality = {k: request[k] for k in ('repository', 'commit', 'run_id', 'run_attempt')}
            return {'schema': 1, 'request': request, 'quality': dict(quality, tier='full', records=records())}

    def test_same_attempt_release_rock(self):
        rock = publication.check_manifest(self.manifest(), 'v0.2.0', ENV['GITHUB_SHA'])
        self.assertEqual(rock['rock'], 'opentlv-0.2.0-1.src.rock')
        bad = [('tag', 'v0.2.1', None), ('commit', None, 'd' * 40)]
        for name, tag, commit in bad:
            with self.subTest(name=name), self.assertRaises(ValueError):
                publication.check_manifest(self.manifest(), tag or 'v0.2.0', commit or ENV['GITHUB_SHA'])
        mutations = [lambda m: m['quality'].update(run_attempt='2'),
                     lambda m: m['quality']['records'][0].update(run_attempt='2'),
                     lambda m: m['quality']['records'][0].update(status='failed'),
                     lambda m: m['quality'].update(tier='pr'),
                     lambda m: next(r for r in m['quality']['records'] if r['release_rock']).update(release_rock=None),
                     lambda m: next(r for r in m['quality']['records'] if r['release_rock'])['release_rock'].update(
                         sha256='9' * 64)]
        for index, mutate in enumerate(mutations):
            manifest = deepcopy(self.manifest())
            mutate(manifest)
            with self.subTest(index=index), self.assertRaises(ValueError):
                publication.check_manifest(manifest, 'v0.2.0', ENV['GITHUB_SHA'])

    def test_manual_main_dispatch_of_this_workflow_only(self):
        good = {'GITHUB_REPOSITORY': 'OpenTLV/opentlv-lua', 'GITHUB_EVENT_NAME': 'workflow_dispatch',
                'GITHUB_REF': 'refs/heads/main', 'GITHUB_WORKFLOW_REF': publication.WORKFLOW}
        with patch.dict(os.environ, good):
            publication.check_dispatch()
        for key, value in {'GITHUB_EVENT_NAME': 'workflow_run', 'GITHUB_REF': 'refs/heads/topic',
                           'GITHUB_REPOSITORY': 'fork/opentlv-lua',
                           'GITHUB_WORKFLOW_REF': 'OpenTLV/opentlv-lua/.github/workflows/release.yml@refs/heads/main'
                           }.items():
            with patch.dict(os.environ, good | {key: value}), self.subTest(key=key), self.assertRaises(ValueError):
                publication.check_dispatch()

    def test_credential_scope(self):
        def verify(tag, directory):
            self.assertNotIn('LUAROCKS_API_KEY', os.environ)
            return {'rockspec': ROCKSPEC, 'rockspec_sha256': 'b' * 64, 'rock': ROCK, 'rock_sha256': 'c' * 64}
        with tempfile.TemporaryDirectory() as temp, patch.object(publication, 'verify', verify), \
                patch.object(publication, 'publish', return_value={'status': 'published', 'module_url': 'u'}):
            for command, key in (('verify', KEY), ('publish', '')):
                with patch.dict(os.environ, {'LUAROCKS_API_KEY': key}), \
                        patch.object(sys, 'argv', ['luarocks_release.py', command, '--output', temp]), \
                        self.subTest(command=command), self.assertRaises(ValueError):
                    publication.main()
            with patch.dict(os.environ, {'LUAROCKS_API_KEY': KEY}), \
                    patch.object(sys, 'argv', ['luarocks_release.py', 'publish', '--tag', 'v0.3.0', '--output', temp]):
                publication.main()
                self.assertNotIn('LUAROCKS_API_KEY', os.environ)


if __name__ == '__main__':
    unittest.main()
