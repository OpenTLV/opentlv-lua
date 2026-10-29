# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Release only the dispatch commit after same-attempt Lua and Core verification."""

import argparse
from datetime import date
import json
import os
from pathlib import Path
import re
import shutil
import tempfile

from ci_support import ROOT, digest, identity, read, require, run, same_identity, write
from lua_package import PACKAGE_TESTS
from lua_policy import PACKAGES, configuration, gate, matrix, validate_inventory, check_results
from release_support import TAG, api, attempt_jobs, request_identity, verify_remote
from verified_core import authenticated_release, checksums, lock, safe_name

REPOSITORY = 'OpenTLV/opentlv-lua'
SIGNER = REPOSITORY + '/.github/workflows/release.yml'


def closed_changelog(text, tag):
    require('unreleased' not in text.lower(), 'Close the changelog before release')
    sections = re.split(r'^## ', text, flags=re.MULTILINE)
    require(len(sections) > 1, 'Missing release changelog')
    heading, _, body = sections[1].partition('\n')
    match = re.fullmatch(r'\[' + re.escape(tag) + r'\] - (\d{4}-\d{2}-\d{2})', heading.strip())
    require(match is not None, 'First changelog heading must match the release tag')
    date.fromisoformat(match[1])
    require(re.search(r'^- \S', body, re.MULTILINE), 'Empty release changelog')


def preflight(tag):
    require(re.fullmatch(TAG, tag), 'Tag must be vMAJOR.MINOR.PATCH')
    selected = identity()
    request = request_identity(tag[1:], selected['commit'])
    require(request['lua_sha'] == request['workflow_sha'] == selected['commit'], 'Release requires dispatch SHA')
    require(not run('git', 'status', '--porcelain', capture=True), 'Dirty release checkout')
    closed_changelog((ROOT / 'CHANGELOG.md').read_text(encoding='utf-8'), tag)
    verify_remote(request)
    request.update(selected)
    request['core_lock'] = lock(True)
    (ROOT / 'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='release-core-', dir=ROOT / 'build') as temp:
        _, _, _, request['core_release'] = authenticated_release(Path(temp) / 'assets', request['core_lock'])
    return request


def check_quality(quality, request):
    same_identity(quality)
    require(quality.get('schema') == 1 and quality.get('tier') == 'full', 'Full Lua evidence required')
    require(quality.get('configuration') == configuration(), 'Tested configuration changed')
    check_results(quality.get('checks', {}), 'full')
    validate_inventory(quality.get('records', []), 'full', release=True,
                       core_lock=request['core_lock'], core_release=request['core_release'])
    require(all(r['release_rock']['tag'] == request['tag'] for r in quality['records'] if r.get('release_rock')),
            'Release rocks must be built for the requested tag')


def check_package(record, request):
    package = record['package']
    safe_name(package.get('filename'))
    require(package.get('id') in PACKAGES and package.get('tests') == list(PACKAGE_TESTS) and
            package.get('query') is True and record['shared'] == 'OFF' and
            record['requested_runtime'] == '5.4' and record['compiler']['SIZEOF_DATA_PTR'] == '8' and
            not record['sanitizer'] and record['configuration'] == 'Release', 'Invalid module package evidence')
    require(record.get('architecture', '').lower() in ('amd64', 'x86_64') and
            record.get('system') == ('Windows' if package['id'].startswith('windows-') else 'Linux'),
            'Wrong package architecture/platform')
    metadata = package.get('metadata', {})
    require(metadata.get('binding_version') == request['version'] and
            metadata.get('release_tag') == request['tag'] and
            metadata.get('binding_revision') == request['commit'] and metadata.get('dirty') is False,
            'Wrong module version metadata')
    require(re.fullmatch('[0-9a-f]{64}', package.get('sha256', '')), 'Invalid module digest')


def assemble(request, quality, evidence, output):
    require(request == preflight(request['tag']), 'Release request or Core release identity changed')
    check_quality(quality, request)
    output.mkdir(parents=True, exist_ok=False)
    packages = [r for r in quality['records'] if r.get('package')]
    require(len(packages) == len(PACKAGES) and {r['package']['id'] for r in packages} == set(PACKAGES),
            'Incomplete module packages')
    for record in packages:
        check_package(record, request)
        item = record['package']
        paths = list(evidence.rglob(item['filename']))
        require(len(paths) == 1 and digest(paths[0]) == item['sha256'], 'Missing, duplicate or modified package')
        shutil.copyfile(paths[0], output / item['filename'])
    write(output / 'release-manifest.json', {'schema': 1, 'request': request, 'quality': quality})
    paths = sorted(output.iterdir())
    (output / 'SHA256SUMS').write_text(''.join(f'{digest(p)}  {p.name}\n' for p in paths), encoding='utf-8')


def verify_payload(directory):
    manifest = read(directory / 'release-manifest.json')
    require(manifest.get('schema') == 1, 'Invalid Lua manifest schema')
    request = manifest['request']
    require(request == preflight(request['tag']), 'Release request changed before publication')
    quality = manifest['quality']
    check_quality(quality, request)
    packages = [r for r in quality['records'] if r.get('package')]
    require(len(packages) == len(PACKAGES) and {r['package']['id'] for r in packages} == set(PACKAGES),
            'Incomplete module inventory')
    expected = {'release-manifest.json', *(r['package']['filename'] for r in packages)}
    sums = checksums(directory / 'SHA256SUMS')
    require(set(sums) == expected and {p.name for p in directory.iterdir()} == expected | {'SHA256SUMS'},
            'Unexpected release files')
    for record in packages:
        check_package(record, request)
        require(sums[record['package']['filename']] == record['package']['sha256'], 'Package digest changed')
    for name, checksum in sums.items():
        require(digest(directory / name) == checksum, 'Modified release asset')
    for path in sorted(directory.iterdir()):
        run('gh', 'attestation', 'verify', path, '--repo', REPOSITORY,
            '--signer-workflow', SIGNER, '--source-digest', request['commit'])
    return request


def publish(directory):
    request = verify_payload(directory)
    require(os.environ.get('FULL_CI_RESULT') == os.environ.get('ASSEMBLE_RESULT') == 'success',
            'Required release jobs failed')
    jobs = attempt_jobs(request)
    for suffix in ('Lua quality gate', 'Assemble and attest Lua release'):
        selected = [j for j in jobs if j['name'] == suffix or j['name'].endswith(' / ' + suffix)]
        require(len(selected) == 1 and selected[0].get('conclusion') == 'success' and
                str(selected[0].get('run_attempt')) == request['run_attempt'] and
                selected[0].get('head_sha') == request['commit'], 'Missing same-attempt release job: ' + suffix)
    created = api(REPOSITORY + '/git/refs', {'ref': 'refs/tags/' + request['tag'], 'sha': request['commit']})
    require(created.get('object', {}).get('sha') == request['commit'], 'Unexpected created tag')
    run('gh', 'release', 'create', request['tag'], '--repo', REPOSITORY, '--verify-tag',
        '--title', 'OpenTLV Lua ' + request['version'], '--generate-notes', *sorted(directory.iterdir()))
    released = api(REPOSITORY + '/releases/tags/' + request['tag'])
    require(released.get('tag_name') == request['tag'] and released.get('draft') is False and
            released.get('prerelease') is False and bool(released.get('published_at')) and
            type(released.get('id')) is int and released['id'] > 0, 'GitHub release was not published')
    write(ROOT / 'build/docs-release/release.json', {
        'schema': 1, 'kind': 'component-release', 'status': 'published', 'source': 'lua',
        'repository': REPOSITORY, 'commit': request['commit'], 'workflow_commit': request['workflow_sha'],
        'tag': request['tag'], 'version': request['version'], 'run_id': int(request['run_id']),
        'run_attempt': int(request['run_attempt']), 'release_id': released['id']})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('matrix', 'gate', 'prepare', 'assemble', 'publish'))
    parser.add_argument('--tier', choices=('pr', 'main', 'full'), default='full')
    parser.add_argument('--tag', default=os.environ.get('RELEASE_TAG', ''))
    parser.add_argument('--evidence', type=Path, default=Path('build/evidence'))
    parser.add_argument('--output', type=Path, default=Path('build/release'))
    args = parser.parse_args()
    if args.command == 'matrix':
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as handle:
            handle.write('matrix=' + json.dumps({'include': matrix(args.tier)}) + '\n')
    elif args.command == 'gate':
        records = [read(p) for p in args.evidence.rglob('record.json')]
        write(args.output, gate(args.tier, json.loads(os.environ['CHECK_RESULTS']), records))
    elif args.command == 'prepare':
        write(args.output, preflight(args.tag))
    elif args.command == 'assemble':
        assemble(read(args.evidence / 'request.json'), read(args.evidence / 'quality.json'), args.evidence, args.output)
    else:
        publish(args.output)


if __name__ == '__main__':
    main()
