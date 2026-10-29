#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Verify a published Lua release and upload its attested rock to LuaRocks."""

import argparse
import json
import os
from pathlib import Path
import re
import secrets
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlencode
from urllib.request import Request, urlopen

from check_release_rock import source_rock
from ci_support import digest, read, require, run, write
import release
from release_support import SHA, TAG, api, attempt_jobs
from verified_core import checksums

PACKAGE = 'opentlv'
ACCOUNT = 'marekcingel'
SERVER = 'https://luarocks.org'
AGENT = 'OpenTLV-Lua-release'
WORKFLOW = release.REPOSITORY + '/.github/workflows/luarocks-release.yml@refs/heads/main'
RELEASE_JOBS = ('Lua quality gate', 'Assemble and attest Lua release', 'Publish verified Lua release')
ABIS = ('5.1', '5.2', '5.3', '5.4')


def check_dispatch():
    require(os.environ.get('GITHUB_REPOSITORY') == release.REPOSITORY and
            os.environ.get('GITHUB_EVENT_NAME') == 'workflow_dispatch' and
            os.environ.get('GITHUB_REF') == 'refs/heads/main' and os.environ.get('GITHUB_WORKFLOW_REF') == WORKFLOW,
            'Dispatch luarocks-release.yml manually from main')


def tag_commit(tag):
    target = api(f'{release.REPOSITORY}/git/ref/tags/{tag}')['object']
    for _ in range(10):
        if target['type'] != 'tag':
            break
        target = api(f'{release.REPOSITORY}/git/tags/{target["sha"]}')['object']
    require(target['type'] == 'commit' and re.fullmatch(SHA, target['sha']), 'Cannot resolve the release tag')
    return target['sha']


def check_manifest(manifest, tag, commit):
    """Return the source rock that the release attempt tested on every static-Core configuration."""
    require(manifest.get('schema') == 1, 'Invalid Lua manifest schema')
    request, quality = manifest.get('request', {}), manifest.get('quality', {})
    attempt = {key: request.get(key) for key in ('repository', 'commit', 'run_id', 'run_attempt')}
    require(request.get('tag') == tag and request.get('version') == tag[1:] and
            request.get('commit') == request.get('lua_sha') == commit and quality.get('tier') == 'full' and
            all(quality.get(key) == value for key, value in attempt.items()),
            'The release manifest does not describe this tag')
    records = quality.get('records') or []
    require(records and all(r.get('status') == 'passed' and all(r.get(k) == v for k, v in attempt.items())
                            for r in records), 'Full evidence is not from the release attempt')
    require(all(r.get('release_rock') for r in records if r.get('luarocks') is True and r.get('shared') == 'OFF'),
            'A static-Core LuaRocks configuration lacks release rock evidence')
    return release.release_rock(quality, request)


def verify(tag, directory):
    """Download, authenticate and rebuild the release rock of a published tag."""
    check_dispatch()
    require(re.fullmatch(TAG, tag), 'Tag must be vMAJOR.MINOR.PATCH')
    commit = tag_commit(tag)
    github_release = api(f'{release.REPOSITORY}/releases/tags/{tag}')
    require(github_release.get('tag_name') == tag and github_release.get('draft') is False and
            github_release.get('prerelease') is False and bool(github_release.get('published_at')),
            'The GitHub release is not published')
    directory.mkdir(parents=True, exist_ok=False)
    run('gh', 'release', 'download', tag, '--repo', release.REPOSITORY, '--dir', directory)
    manifest = read(directory / 'release-manifest.json')
    rock = check_manifest(manifest, tag, commit)
    rockspec = rock['rock'].removesuffix('.src.rock') + '.rockspec'
    sums = checksums(directory / 'SHA256SUMS')
    require({'release-manifest.json', rock['rock'], rockspec} <= set(sums) and
            {p.name for p in directory.iterdir()} == set(sums) | {'SHA256SUMS'}, 'Unexpected release assets')
    for name, checksum in sums.items():
        require(digest(directory / name) == checksum, 'Modified release asset')
    require(sums[rock['rock']] == rock['sha256'] and sums[rockspec] == rock['rockspec_sha256'],
            'The release rock differs from the tested rock')
    for path in sorted(directory.iterdir()):
        run('gh', 'attestation', 'verify', path, '--repo', release.REPOSITORY,
            '--signer-workflow', release.SIGNER, '--source-digest', commit)
    request = manifest['request']
    jobs = attempt_jobs(request, require_active=False)
    for suffix in RELEASE_JOBS:
        selected = [j for j in jobs if j['name'] == suffix or j['name'].endswith(' / ' + suffix)]
        require(len(selected) == 1 and selected[0].get('conclusion') == 'success' and
                str(selected[0].get('run_attempt')) == request['run_attempt'] and
                selected[0].get('head_sha') == commit, 'Missing same-attempt release job: ' + suffix)
    with tempfile.TemporaryDirectory(prefix='luarocks-rebuild-') as temp:
        rebuilt = source_rock(Path(temp), commit, tag)
        require([digest(p) for p in rebuilt] == [rock['rockspec_sha256'], rock['sha256']],
                'The release rock is not reproducible from its tag')
    return {'tag': tag, 'commit': commit, 'version': rock['rock'][len(PACKAGE) + 1:-len('.src.rock')],
            'release_id': github_release['id'], 'run_id': request['run_id'], 'run_attempt': request['run_attempt'],
            'rockspec': rockspec, 'rockspec_sha256': rock['rockspec_sha256'],
            'rock': rock['rock'], 'rock_sha256': rock['sha256']}


def multipart(field, path):
    boundary = 'OpenTLV' + secrets.token_hex(16)
    data = path.read_bytes()
    require(boundary.encode() not in data, 'Multipart boundary collision')
    body = (f'--{boundary}\r\nContent-Disposition: form-data; name="{field}"; filename="{path.name}"\r\n'
            'Content-Type: application/octet-stream\r\n\r\n').encode() + data + f'\r\n--{boundary}--\r\n'.encode()
    return body, 'multipart/form-data; boundary=' + boundary


class LuaRocks:
    """The luarocks.org upload API used by `luarocks upload`, keeping the key out of argv and messages."""

    def __init__(self, key):
        # The key is a URL path segment of every API call; never echo a URL.
        require(re.fullmatch(r'[A-Za-z0-9_-]{16,}', key), 'A valid LUAROCKS_API_KEY is required')
        self._key = key

    def call(self, method, params=None, field=None, path=None):
        url = f'{SERVER}/api/1/{self._key}/{method}' + ('?' + urlencode(params) if params else '')
        headers = {'User-Agent': AGENT, 'Accept': 'application/json'}
        data = None
        if path is not None:
            data, headers['Content-Type'] = multipart(field, path)
        label = method.split('/')[0]
        try:
            with urlopen(Request(url, data=data, headers=headers), timeout=120) as response:
                result = json.load(response)
        except HTTPError as error:
            raise ValueError(f'LuaRocks {label} failed (HTTP {error.code})') from None
        except (URLError, TimeoutError, json.JSONDecodeError):
            raise ValueError(f'LuaRocks {label} failed: no valid response') from None
        require(isinstance(result, dict), f'LuaRocks {label} returned an invalid response')
        errors = result.get('errors')
        require(not errors, f'LuaRocks {label} rejected the request')
        return result


def fetch(url):
    """Return a public luarocks.org file, or None when it does not exist."""
    try:
        with urlopen(Request(url, headers={'User-Agent': AGENT}), timeout=120) as response:
            return response.read()
    except HTTPError as error:
        if error.code == 404:
            return None
        raise ValueError(f'Cannot read {url} (HTTP {error.code})') from None
    except (URLError, TimeoutError):
        raise ValueError(f'Cannot read {url}') from None


def published(name):
    return fetch(f'{SERVER}/manifests/{ACCOUNT}/{quote(name)}')


def name_taken():
    for abi in ABIS:
        manifest = fetch(f'{SERVER}/manifest-{abi}.json')
        require(manifest is not None, 'Cannot read the LuaRocks root manifest')
        if PACKAGE in json.loads(manifest).get('repository', {}):
            return True
    return False


def publish(plan, directory, client, attempts=6, delay=10):
    """Upload the verified rock once; an existing version is never replaced."""
    rockspec, rock = directory / plan['rockspec'], directory / plan['rock']
    state = client.call('check_rockspec', {'package': PACKAGE, 'version': plan['version']})
    version = state.get('version')
    if version:
        require(published(rockspec.name) == rockspec.read_bytes(),
                f'LuaRocks already has a different {rockspec.name}; versions are immutable')
        status = 'already-published'
    else:
        require(state.get('module') or not name_taken(),
                f'Another account owns {PACKAGE} in the LuaRocks root manifest')
        uploaded = client.call('upload', field='rockspec_file', path=rockspec)
        require(not uploaded.get('is_new') or uploaded.get('manifests'),
                f'LuaRocks did not add {PACKAGE} to the root manifest')
        version = uploaded.get('version') or {}
        status = 'published'
    remote = published(rock.name)
    if remote is None:
        require(type(version.get('id')) is int, 'LuaRocks returned no version id for the source rock')
        client.call(f'upload_rock/{version["id"]}', field='rock_file', path=rock)
        status = 'published'
    else:
        require(remote == rock.read_bytes(), f'LuaRocks already has a different {rock.name}; versions are immutable')
    for attempt in range(attempts):
        if published(rockspec.name) == rockspec.read_bytes() and published(rock.name) == rock.read_bytes():
            break
        require(attempt + 1 < attempts, 'LuaRocks does not serve the uploaded files')
        time.sleep(delay)
    return {'schema': 1, 'kind': 'luarocks-release', 'status': status, 'package': PACKAGE, 'account': ACCOUNT,
            'server': SERVER, **{k: plan[k] for k in ('tag', 'commit', 'version', 'release_id', 'run_id',
                                                      'run_attempt', 'rockspec', 'rockspec_sha256',
                                                      'rock', 'rock_sha256')},
            'module_url': f'{SERVER}/modules/{ACCOUNT}/{PACKAGE}/{plan["version"]}'}


def main():
    # Child processes (git, gh) never receive the registry credential.
    key = os.environ.pop('LUAROCKS_API_KEY', '').strip()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('verify', 'publish'))
    parser.add_argument('--tag', default=os.environ.get('RELEASE_TAG', ''))
    parser.add_argument('--output', type=Path, default=Path('build/luarocks'))
    args = parser.parse_args()
    if args.command == 'verify':
        require(not key, 'Verification must run without the LuaRocks credential')
    else:
        client = LuaRocks(key)
    plan = verify(args.tag, args.output / 'release')
    write(args.output / 'plan.json', plan)
    print(f"Verified {plan['rockspec']} ({plan['rockspec_sha256']}) and {plan['rock']} ({plan['rock_sha256']})")
    if args.command == 'publish':
        receipt = publish(plan, args.output / 'release', client)
        write(args.output / 'receipt.json', receipt)
        print(f"LuaRocks {receipt['status']}: {receipt['module_url']}")


if __name__ == '__main__':
    main()
