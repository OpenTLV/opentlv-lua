# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Deterministic Lua release notes; rendering is not publication authorization."""

import argparse
from datetime import date
import html
from pathlib import Path
import re

from ci_support import ROOT, read, require
from lua_package import PACKAGE_TESTS
from lua_policy import PACKAGES, check_results, matrix
from release_support import REPOSITORY, SHA, TAG
from verified_core import safe_name

BASE = 'https://github.com/' + REPOSITORY
CATEGORIES = ('Added', 'Changed', 'Deprecated', 'Removed', 'Fixed', 'Security')
SECTIONS = ('Overview', "What's Changed", 'Compatibility', 'Installation / Distribution',
            'Verification and artifacts', 'Links')
NOTES = 'release-notes.md'


def plain(text):
    """Keep editorial prose and evidence values literal in Markdown."""
    require(isinstance(text, str) and text.strip() and not any(ord(c) < 32 for c in text),
            'Expected nonempty single-line text')
    return re.sub(r'([\\`*_{}\[\]()#+.!|>-])', r'\\\1', html.escape(text.strip(), quote=False))


def overview(editorial, tag):
    require(re.fullmatch(TAG, tag), 'Release notes require a vMAJOR.MINOR.PATCH tag')
    require(set(editorial) == {'schema', 'version', 'overview'} and
            editorial['schema'] == 1 and
            editorial['version'] == tag[1:], 'Editorial overview must identify this release version')
    text = plain(editorial['overview'])
    require(not re.search(r'OpenTLV\s+Lua\s+v?' + re.escape(tag[1:]) + r'\b', editorial['overview'], re.IGNORECASE),
            'The overview must describe the changes rather than repeat the release title')
    return text


def changes(changelog, tag, *, historical=False):
    """Retain approved Markdown and omit empty categories; legacy headings are read-only."""
    version = re.escape(tag[1:])
    candidates = []
    for section in re.split(r'^## ', changelog, flags=re.MULTILINE)[1:]:
        heading, _, body = section.partition('\n')
        match = re.fullmatch(r'\[' + ('v?' if historical else '') + version + r'\] - (\d{4}-\d{2}-\d{2})',
                             heading)
        if match:
            date.fromisoformat(match[1])
            candidates.append(body)
    require(len(candidates) == 1, 'Expected exactly one changelog section for the release')
    parts = re.split(r'^### ', candidates[0], flags=re.MULTILINE)
    require(not parts[0].strip(), 'Release changes must be in named changelog categories')
    groups = {}
    for part in parts[1:]:
        category, _, body = part.partition('\n')
        require(category in CATEGORIES and category not in groups, 'Unknown or duplicate changelog category')
        body = body.strip()
        require(not body or body.startswith('- '), 'Changelog categories must contain Markdown list items')
        # Relative links and reference-style links lose their context in a release body.
        # Authors use explicit HTTPS links in release changelog entries instead.
        for target in re.findall(r'\]\(([^)]+)\)', body):
            require(target.startswith('https://') and not re.search(r'\s', target),
                    'Use absolute HTTPS changelog links for release notes')
        require(not re.search(r'\]\[[^\]]*\]', body), 'Use inline HTTPS changelog links for release notes')
        groups[category] = re.sub(
            r'(?m)^(- .*)\((OpenTLV/[A-Za-z0-9_.-]+)?#([1-9][0-9]*)\)$',
            lambda m: f'{m[1]}([{m[2] or ""}#{m[3]}](https://github.com/{m[2] or REPOSITORY}/issues/{m[3]}))', body)
    require(any(groups.values()), 'Release changelog has no changes')
    return '\n\n'.join('### ' + name + '\n\n' + groups[name] for name in CATEGORIES if groups.get(name))


def core_identity(selected):
    require(selected.get('repository') == 'OpenTLV/opentlv' and
            re.fullmatch(SHA, selected.get('commit', '')) and
            re.fullmatch(TAG, selected.get('release_tag', '')), 'Missing exact Core release baseline')
    return (f"[Core {selected['release_tag']}](https://github.com/OpenTLV/opentlv/releases/tag/"
            f"{selected['release_tag']}) at `{selected['commit']}`")


def layout(parts):
    return '\n\n'.join('## ' + section + '\n\n' + parts[section] for section in SECTIONS) + '\n'


def preview(changelog, editorial, selected, tag):
    """Review prose now; no successful run, platform or distribution is invented."""
    return layout({
        'Overview': overview(editorial, tag),
        "What's Changed": changes(changelog, tag),
        'Compatibility': f"Lua binding **{tag[1:]}**. Requested baseline: {core_identity(selected)}.\n\n"
                         'Verified runtimes and platforms will be listed from the successful release Full records.',
        'Installation / Distribution': 'Pending release evidence. The final body will link only the tested GitHub '
                                       'assets. LuaRocks publication requires separate approval and verification; '
                                       'no registry install command is promised here.',
        'Verification and artifacts': 'Preview only: Full CI, artifact hashes and attestations are pending. '
                                      'The final body will include the exact commit, run/attempt and tested configurations.',
        'Links': f'[Full changelog]({BASE}/blob/{tag}/CHANGELOG.md) · '
                 f'[Lua API documentation]({BASE}/blob/{tag}/docs/api.md)\n\n'
                 'These candidate links become available when the approved tag is published.'})


def reviewed(root, tag):
    editorial = read(root / 'docs/release-notes.json')
    changelog = (root / 'CHANGELOG.md').read_text(encoding='utf-8')
    expected = preview(changelog, editorial, read(root / 'cmake/core-artifact.json'), tag)
    require((root / 'docs/release-preview.md').read_text(encoding='utf-8') == expected,
            'Release preview is stale: regenerate and review it before dispatch')
    return changelog, editorial


def evidence(manifest, *, historical=False):
    require(manifest.get('schema') == 1, 'Missing Lua release manifest')
    request, quality = manifest['request'], manifest['quality']
    tag, commit = request['tag'], request['commit']
    require(re.fullmatch(TAG, tag) and request['version'] == tag[1:] and re.fullmatch(SHA, commit) and
            request['lua_sha'] == request['workflow_sha'] == commit and
            request['repository'] == REPOSITORY, 'Release notes identity mismatch')
    require(all(re.fullmatch(r'[1-9][0-9]*', str(request[k])) for k in ('run_id', 'run_attempt')),
            'Missing release run/attempt')
    require(quality.get('schema') == 1 and quality.get('tier') == 'full', 'Missing Full evidence')
    check_results(quality.get('checks', {}), 'full')
    fields = ('repository', 'commit', 'run_id', 'run_attempt')
    require(all(quality.get(k) == request[k] for k in fields), 'Full evidence belongs to another attempt')
    selected, authenticated = request['core_lock'], request['core_release']
    core_identity(selected)
    require(type(authenticated.get('release_id')) is int and authenticated['release_id'] > 0 and
            re.fullmatch('[0-9a-f]{64}', authenticated.get('manifest_sha256', '')),
            'Missing verified Core release identity')
    records = quality.get('records', [])
    rows = matrix('full')
    require(len(records) == len(rows) and {r['id'] for r in records} == {r['id'] for r in rows},
            'Incomplete Full configuration inventory')
    by_id = {r['id']: r for r in records}
    artifacts = {}
    rocks = []
    packages = []
    for row in rows:
        r = by_id[row['id']]
        require(r.get('status') == 'passed' and all(r.get(k) == request[k] for k in fields) and
                r.get('lua_sha') == commit, 'Failed or stale Full record')
        system = {'ubuntu-24.04': 'Linux', 'windows-latest': 'Windows', 'macos-latest': 'Darwin'}[row['os']]
        require(r['system'] == system and r['requested_runtime'] == row['lua'] and
                r['shared'] == row['shared'] and r['sanitizer'] == row['sanitizer'] and
                r['configuration'] == row['configuration'] and r['architecture'], 'Wrong configuration evidence')
        require(('LuaJIT' in r['runtime'] if row['lua'] == 'luajit' else r['runtime'] == 'Lua ' + row['lua']),
                'Wrong runtime evidence')
        require(all(type(r.get(k)) is int and r[k] > 0 for k in
                    ('ctest_passed', 'ctest_total', 'busted_passed', 'query_fixtures', 'query_checks')) and
                r['ctest_passed'] == r['ctest_total'] and
                all(r.get(k) == 0 for k in ('busted_failed', 'busted_errors', 'busted_pending')) and
                (not row['rocks'] or r.get('luarocks') is True), 'Incomplete test evidence')
        dep = r.get('core', {})
        require(dep.get('verified') is True and dep.get('mode') in ('verified-source', 'verified-binary') and
                dep.get('commit') == r.get('core_sha') == selected['commit'] and
                dep.get('release_tag') == selected['release_tag'] and
                all(dep.get(k) == v for k, v in authenticated.items()), 'Unverified Core baseline')
        if row['package']:
            p = r.get('package', {})
            require(r['architecture'].lower() in ('x86_64', 'amd64') and
                    r.get('compiler', {}).get('SIZEOF_DATA_PTR') == '8', 'Wrong package architecture')
            require(p.get('id') == row['package'] and p.get('tests') == list(PACKAGE_TESTS) and p.get('query') is True,
                    'Missing relocated package verification')
            require(p.get('metadata') == {'binding_version': tag[1:], 'binding_revision': commit,
                                         'release_tag': tag, 'dirty': False}, 'Package version mismatch')
            name = safe_name(p['filename'])
            require(name == f'OpenTLV-Lua-{tag[1:]}-{p["id"]}.zip', 'Unexpected package filename')
            artifacts[name] = p['sha256']
            packages.append(p)
        if row['rocks'] and row['shared'] == 'OFF':
            rock = r.get('release_rock')
            require(historical or rock, 'Missing release rock evidence')
            if rock:
                rocks.append(rock)
    require({p['id'] for p in packages} == set(PACKAGES), 'Missing release packages')
    if rocks:
        require(len(rocks) == 13 and all(r == rocks[0] for r in rocks), 'Inconsistent release rock evidence')
        rock = rocks[0]
        require(rock['tag'] == tag and rock['commit'] == commit and rock['core_commit'] == selected['commit'] and
                rock['rock'] == f'opentlv-{tag[1:]}-1.src.rock' and rock['query'] is True and
                rock['tests'] == list(PACKAGE_TESTS), 'Wrong release rock identity')
        artifacts[rock['rock']] = rock['sha256']
        artifacts[rock['rock'].replace('.src.rock', '.rockspec')] = rock['rockspec_sha256']
    require(all(re.fullmatch('[0-9a-f]{64}', h) for h in artifacts.values()), 'Missing artifact SHA-256')
    return request, sorted(records, key=lambda r: r['id']), artifacts


def render(manifest, changelog, editorial, *, historical=False):
    request, records, artifacts = evidence(manifest, historical=historical)
    tag, version, commit = request['tag'], request['version'], request['commit']
    download = f'{BASE}/releases/download/{tag}/'
    source = f'{BASE}/blob/{commit}/'
    run_url = f"{BASE}/actions/runs/{request['run_id']}/attempts/{request['run_attempt']}"
    platforms = sorted({f"{r['system']} {r['architecture']} / {r['runtime']}" for r in records})
    compatibility = (f'Lua binding **{version}**, Git tag `{tag}`.\n\nVerified baseline: '
                     + core_identity(request['core_lock']) + '.\n\nTested runtime/platform combinations:\n\n'
                     + '\n'.join('- ' + plain(p) for p in platforms)
                     + '\n\nThese are tested configurations, not a claim of binaries for every platform.')
    installation = ('GitHub module archives contain the public Lua facade, native module and license notices. '
                    'They target the matching Lua 5.4 runtime on Linux/Windows x64.\n\n'
                    + '\n'.join(f'- [{n}]({download}{n})' for n in sorted(artifacts) if n.endswith('.zip')))
    linux = f'OpenTLV-Lua-{version}-linux-x64-lua54.zip'
    installation += ('\n\nAfter downloading and verifying the Linux archive below, extract it in a fresh directory '
                     'and load it with Lua 5.4:\n\n```sh\nunzip ' + linux + ' -d opentlv-lua\n'
                     'LUA_PATH="$PWD/opentlv-lua/?/init.lua" LUA_CPATH="$PWD/opentlv-lua/?.so" \\\n'
                     '    lua -e \'assert(require("opentlv") == require("opentlv._core"))\'\n```')
    if any(n.endswith('.src.rock') for n in artifacts):
        installation += '\n\nThe tested source rock and rockspec are attached to this GitHub Release.'
    installation += ('\n\nLuaRocks registry publication is a separate approved operation. '
                     'These release notes do not establish registry availability.')
    verification = (f"[Release Full CI, attempt {request['run_attempt']}]({run_url}) records "
                    f"{len(records)} passed configurations for Lua commit `{commit}`. "
                    'Required build, source quality, CodeQL and API coverage jobs passed.\n\n'
                    '| Configuration | CTest | Busted | Query fixtures / checks |\n'
                    '| --- | ---: | ---: | ---: |\n' + '\n'.join(
                        f"| {plain(r['id'])} | {r['ctest_passed']}/{r['ctest_total']} | {r['busted_passed']} | "
                        f"{r['query_fixtures']} / {r['query_checks']} |" for r in records)
                    + '\n\nThe release gate verifies checksums, producer attestations and same-attempt jobs '
                    'before publication. Artifact SHA-256 values from the verified build records:\n\n'
                    '| Artifact | SHA-256 |\n| --- | --- |\n' + '\n'.join(
                        f'| [{name}]({download}{name}) | `{checksum}` |' for name, checksum in sorted(artifacts.items()))
                    + f"\n\nCore release ID: `{request['core_release']['release_id']}`; authenticated Core manifest "
                    + f"SHA-256: `{request['core_release']['manifest_sha256']}`.\n\n"
                    + 'Download `SHA256SUMS` and the assets into one directory and check their bytes with '
                    '`sha256sum --check SHA256SUMS`. Verify each asset using, for example:\n\n```sh\n'
                    + f'gh attestation verify {linux} --repo {REPOSITORY} \\\n'
                    + f'    --signer-workflow {REPOSITORY}/.github/workflows/release.yml \\\n'
                    + f'    --source-digest {commit}\n```')
    links = [f'[Full changelog]({source}CHANGELOG.md)', f'[Lua API documentation]({source}docs/api.md)',
             f'[Installation guide]({source}docs/development.md)', f'[Release workflow evidence]({run_url})',
             f'[Release manifest]({download}release-manifest.json)', f'[SHA256SUMS]({download}SHA256SUMS)']
    if not historical:
        links.append(f'[Attested release description]({download}{NOTES})')
    return layout({'Overview': overview(editorial, tag), "What's Changed": changes(
                       changelog, tag, historical=historical),
                   'Compatibility': compatibility, 'Installation / Distribution': installation,
                   'Verification and artifacts': verification, 'Links': '\n'.join('- ' + link for link in links)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('preview', 'render'))
    parser.add_argument('--tag', required=True)
    parser.add_argument('--changelog', type=Path, default=ROOT / 'CHANGELOG.md')
    parser.add_argument('--editorial', type=Path, default=ROOT / 'docs/release-notes.json')
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--historical', action='store_true', help='Read-only rendering of legacy changelog/rock evidence')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    changelog, editorial = args.changelog.read_text(encoding='utf-8'), read(args.editorial)
    if args.command == 'preview':
        require(not args.historical and args.manifest is None, 'Preview uses candidate sources, not release evidence')
        result = preview(changelog, editorial, read(ROOT / 'cmake/core-artifact.json'), args.tag)
    else:
        require(args.manifest is not None, 'Rendering requires an authenticated release manifest')
        manifest = read(args.manifest)
        require(manifest['request']['tag'] == args.tag, 'Manifest tag differs from requested tag')
        result = render(manifest, changelog, editorial, historical=args.historical)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result.encode('utf-8'))


if __name__ == '__main__':
    main()
