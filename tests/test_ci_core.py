# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Core consumer tests authenticate artifacts without rerunning Core semantics."""

import copy
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from ci_support import digest
from verified_core import REQUIRED_FEATURES, checksums, compatible, extract, resolve, verify_manifest

LOCK = {'schema': 1, 'repository': 'OpenTLV/opentlv', 'commit': 'b' * 40,
        'release_tag': 'v0.8.0', 'allow_verified_source': True}


def manifest():
    return {'schema': 2, 'commit': LOCK['commit'], 'version': '0.8.0',
            'candidate': {'repository': LOCK['repository'], 'commit': LOCK['commit'], 'tag': 'v0.8.0',
                          'run_id': '123', 'run_attempt': '1'},
            'quality': {'schema': 2, 'tier': 'full', 'repository': LOCK['repository'],
                        'commit': LOCK['commit'], 'run_id': '123', 'run_attempt': '1',
                        'configuration': {'workflows': {}}, 'checks': {'build': 'success'}},
            'artifacts': [{'filename': 'OpenTLV-0.8.0-source.tar.gz', 'sha256': 'c' * 64,
                           'kind': 'source', 'version': '0.8.0', 'commit': LOCK['commit']}]}


class CoreConsumer(unittest.TestCase):
    def test_manifest_identity_and_full_evidence(self):
        good = manifest()
        verify_manifest(good, LOCK)
        for field, value in [('schema', 1), ('commit', 'a' * 40), ('version', '0.7.1')]:
            with self.assertRaises(ValueError):
                verify_manifest(good | {field: value}, LOCK)
        for field, value in [('tier', 'pr'), ('repository', 'other/repo'), ('run_attempt', '2'),
                             ('checks', {'build': 'skipped'}), ('configuration', {})]:
            invalid = copy.deepcopy(good)
            invalid['quality'][field] = value
            with self.assertRaises(ValueError):
                verify_manifest(invalid, LOCK)

    def test_duplicate_and_traversal_artifacts(self):
        invalid = manifest()
        invalid['artifacts'] *= 2
        with self.assertRaises(ValueError):
            verify_manifest(invalid, LOCK)
        invalid = manifest()
        invalid['artifacts'][0]['filename'] = '../escape'
        with self.assertRaises(ValueError):
            verify_manifest(invalid, LOCK)

    def test_archive_traversal_links_and_devices_rejected(self):
        for kind in ('traversal', 'symlink', 'device'):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                archive = root / 'bad.tar.gz'
                with tarfile.open(archive, 'w:gz') as stream:
                    member = tarfile.TarInfo('../escape' if kind == 'traversal' else 'entry')
                    if kind == 'symlink':
                        member.type, member.linkname = tarfile.SYMTYPE, '../../escape'
                    elif kind == 'device':
                        member.type = tarfile.CHRTYPE
                    stream.addfile(member)
                with self.subTest(kind=kind), self.assertRaises(ValueError):
                    extract(archive, root / 'out')

    def test_zip_traversal_and_duplicate_checksums(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with zipfile.ZipFile(root / 'bad.zip', 'w') as archive:
                archive.writestr('../escape', 'bad')
            with self.assertRaises(ValueError):
                extract(root / 'bad.zip', root / 'out')
            (root / 'sums').write_text(('a' * 64 + '  asset.tar.gz\n') * 2)
            with self.assertRaises(ValueError):
                checksums(root / 'sums')

    def test_binary_requires_toolchain_runtime_linkage_and_architecture(self):
        probe = {'COMPILER_ID': 'GNU', 'COMPILER_VERSION': '13.3.0', 'SIZEOF_DATA_PTR': '8'}
        record = {'id': 'linux-x64-shared', 'architecture': 'x64', 'os': 'linux',
                  'build_type': 'Release', 'linkage': 'shared',
                  'compiler': {'CMAKE_C_COMPILER_ID': 'GNU', 'CMAKE_C_COMPILER_VERSION': '13.3.0'},
                  'libc': ['glibc', '2.39'], 'options': dict.fromkeys(REQUIRED_FEATURES, 'ON')}
        with patch('verified_core.platform.system', return_value='Linux'), \
                patch('verified_core.platform.machine', return_value='x86_64'), \
                patch('verified_core.platform.libc_ver', return_value=('glibc', '2.39')):
            self.assertTrue(compatible(record, probe, 'ON', 'Release'))
            self.assertFalse(compatible(record, probe, 'OFF', 'Release'))
            self.assertFalse(compatible(record, probe, 'ON', 'Debug'))
            self.assertFalse(compatible(record, probe | {'COMPILER_ID': 'Clang'}, 'ON', 'Release'))
            self.assertFalse(compatible(record | {'libc': ['glibc', '2.31']}, probe, 'ON', 'Release'))
            for feature in REQUIRED_FEATURES:
                for value in ('OFF', None):
                    options = record['options'].copy()
                    if value is None:
                        del options[feature]
                    else:
                        options[feature] = value
                    with self.subTest(feature=feature, value=value):
                        self.assertFalse(compatible(record | {'options': options}, probe, 'ON', 'Release'))

    def test_attestation_failure_never_becomes_source_fallback(self):
        with tempfile.TemporaryDirectory() as tmp, patch('verified_core.lock', return_value=LOCK), \
                patch('verified_core.api', side_effect=[{'id': 1, 'tag_name': 'v0.8.0', 'draft': False,
                                                        'assets': [{'name': 'release-manifest.json'}]},
                                                       {'sha': LOCK['commit']}]), \
                patch('verified_core.run') as command:
            def fail(*args, **kwargs):
                if args[:3] == ('gh', 'attestation', 'verify'):
                    raise RuntimeError('invalid producer')
            command.side_effect = fail
            with self.assertRaisesRegex(RuntimeError, 'invalid producer'):
                resolve(Path(tmp) / 'core', 'ON', 'Release', required=True)
            self.assertEqual(command.call_count, 2)

    def test_authenticated_source_resolution_and_corrupt_archive(self):
        for corrupt in (False, True):
            with self.subTest(corrupt=corrupt), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                data = manifest()
                name = data['artifacts'][0]['filename']
                archive = root / name
                with tarfile.open(archive, 'w:gz') as stream:
                    content = f"version=0.8.0\ncommit={LOCK['commit']}\n".encode()
                    entry = tarfile.TarInfo('OpenTLV-0.8.0/opentlv-source.txt')
                    entry.size = len(content)
                    stream.addfile(entry, io.BytesIO(content))
                data['artifacts'][0]['sha256'] = digest(archive)
                (root / 'release-manifest.json').write_text(json.dumps(data))
                (root / 'SHA256SUMS').write_text(
                    f"{digest(root / 'release-manifest.json')}  release-manifest.json\n"
                    f"{digest(archive)}  {name}\n")
                if corrupt:
                    archive.write_bytes(b'changed')
                release = {'id': 7, 'tag_name': LOCK['release_tag'], 'draft': False,
                           'assets': [{'name': n} for n in ('release-manifest.json', 'SHA256SUMS', name)]}
                calls = []

                def command(*args, **kwargs):
                    calls.append(args)
                    if args[:3] == ('gh', 'release', 'download'):
                        filename = args[args.index('--pattern') + 1]
                        destination = args[args.index('--dir') + 1]
                        (destination / filename).write_bytes((root / filename).read_bytes())

                with patch('verified_core.lock', return_value=LOCK), \
                        patch('verified_core.api', side_effect=[release, {'sha': LOCK['commit']}]), \
                        patch('verified_core.run', side_effect=command), \
                        patch('verified_core.compiler', return_value={}):
                    if corrupt:
                        with self.assertRaisesRegex(ValueError, 'checksum'):
                            resolve(root / 'work', 'ON', 'Debug', required=True, source_only=True)
                    else:
                        options, evidence = resolve(root / 'work', 'ON', 'Debug', required=True, source_only=True)
                        self.assertEqual(evidence['mode'], 'verified-source')
                        self.assertTrue(evidence['verified'])
                        self.assertIn('FETCHCONTENT_SOURCE_DIR_OPENTLV_CORE=', options[0])
                        attestations = [c for c in calls if c[:3] == ('gh', 'attestation', 'verify')]
                        self.assertEqual(len(attestations), 3)
                        for call in attestations:
                            self.assertEqual(call[call.index('--source-digest') + 1], LOCK['commit'])

    def test_verified_binary_selection_has_no_source_or_git_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = root / 'OpenTLV-0.8.0-windows-x64-static.zip'
            with zipfile.ZipFile(archive, 'w') as stream:
                stream.writestr('OpenTLV/lib/cmake/OpenTLV/OpenTLVConfig.cmake', '# controlled fixture\n')
            record = {'kind': 'binary', 'id': 'windows-x64-static', 'filename': archive.name,
                      'sha256': digest(archive)}

            source = root / 'source.tar.gz'
            with tarfile.open(source, 'w:gz') as stream:
                content = f"version=0.8.0\ncommit={LOCK['commit']}\n".encode()
                entry = tarfile.TarInfo('OpenTLV-0.8.0/opentlv-source.txt')
                entry.size = len(content)
                stream.addfile(entry, io.BytesIO(content))
            source_record = {'kind': 'source', 'filename': source.name, 'sha256': digest(source)}

            def authenticated(work, selected):
                work.mkdir()
                return ({'version': '0.8.0', 'artifacts': [record, source_record]},
                        {archive.name: digest(archive), source.name: digest(source)},
                        lambda name: root / name, {'release_id': 7, 'manifest_sha256': 'd' * 64})

            with patch('verified_core.lock', return_value=LOCK), \
                    patch('verified_core.authenticated_release', side_effect=authenticated), \
                    patch('verified_core.compatible', side_effect=lambda r, *args: r.get('kind') == 'binary'), \
                    patch('verified_core.compiler', return_value={}), patch('verified_core.run'):
                options, evidence = resolve(root / 'work', 'OFF', 'Release', required=True)
                self.assertEqual(evidence['mode'], 'verified-binary')
                self.assertEqual(len(options), 1)
                self.assertTrue(options[0].startswith('-DOPENTLV_LUA_CORE_PACKAGE='))

    def test_source_fallback_can_be_explicitly_disabled(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)

            def authenticated(work, selected):
                work.mkdir()
                return manifest(), {}, None, {}

            with patch('verified_core.lock', return_value=LOCK | {'allow_verified_source': False}), \
                    patch('verified_core.authenticated_release', side_effect=authenticated), \
                    patch('verified_core.compiler', return_value={}), patch('verified_core.run'):
                with self.assertRaisesRegex(ValueError, 'fallback disabled'):
                    resolve(root / 'work', 'OFF', 'Debug', required=True, source_only=True)

    def test_static_module_requires_declared_pic_and_macos_never_uses_linux_binary(self):
        probe = {'COMPILER_ID': 'GNU', 'COMPILER_VERSION': '13.3.0', 'SIZEOF_DATA_PTR': '8'}
        record = {'id': 'linux-x64-static', 'architecture': 'x64', 'os': 'linux',
                  'build_type': 'Release', 'linkage': 'static',
                  'compiler': {'CMAKE_C_COMPILER_ID': 'GNU', 'CMAKE_C_COMPILER_VERSION': '13.3.0'},
                  'libc': ['glibc', '2.39'], 'options': dict.fromkeys(REQUIRED_FEATURES, 'ON')}
        with patch('verified_core.platform.system', return_value='Linux'), \
                patch('verified_core.platform.machine', return_value='x86_64'), \
                patch('verified_core.platform.libc_ver', return_value=('glibc', '2.39')):
            self.assertFalse(compatible(record, probe, 'OFF', 'Release'))
            self.assertTrue(compatible(record | {'position_independent_code': True}, probe, 'OFF', 'Release'))
        with patch('verified_core.platform.system', return_value='Darwin'):
            self.assertFalse(compatible(record | {'position_independent_code': True}, probe, 'OFF', 'Release'))

    def test_unset_release_lock_is_development_only(self):
        from verified_core import lock
        with patch('verified_core.read', return_value=LOCK | {'release_tag': None}), \
                patch('verified_core.pin', return_value=LOCK['commit']):
            self.assertIsNone(lock()['release_tag'])
            with self.assertRaisesRegex(ValueError, 'before Lua release'):
                lock(True)


if __name__ == '__main__':
    unittest.main()
