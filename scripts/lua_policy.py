# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Declared Lua verification configurations and fail-closed evidence checks."""

from ci_support import ROOT, digest, identity, require, same_identity
from lua_suite import EXPECTED_TESTS
from check_dependency import SCENARIOS

SYSTEMS = ('ubuntu-24.04', 'windows-latest', 'macos-latest')
RUNTIMES = ('5.1', '5.2', '5.3', '5.4')
PACKAGES = ('linux-x64-lua54', 'windows-x64-lua54')


def matrix(tier):
    rows = [{'os': system, 'lua': lua, 'shared': 'OFF', 'sanitizer': False}
            for system in SYSTEMS for lua in RUNTIMES] if tier == 'full' else [
                {'os': 'ubuntu-24.04', 'lua': '5.4', 'shared': 'OFF', 'sanitizer': False}]
    if tier == 'full':
        rows += [{'os': 'ubuntu-24.04', 'lua': 'luajit', 'shared': 'OFF', 'sanitizer': False}]
        rows += [{'os': system, 'lua': '5.4', 'shared': 'ON', 'sanitizer': False} for system in SYSTEMS]
        rows += [{'os': 'ubuntu-24.04', 'lua': '5.4', 'shared': 'OFF', 'sanitizer': True}]
    for row in rows:
        row['id'] = f"{row['os']}-{row['lua']}-{row['shared']}" + ('-asan' if row['sanitizer'] else '')
        row['configuration'] = 'Debug' if row['sanitizer'] or tier == 'pr' else 'Release'
        row['rocks'] = tier == 'full' and not row['sanitizer']
        row['package'] = ''
        if tier == 'full' and row['lua'] == '5.4' and row['shared'] == 'OFF' and not row['sanitizer']:
            row['package'] = {'ubuntu-24.04': PACKAGES[0], 'windows-latest': PACKAGES[1]}.get(row['os'], '')
    return rows


def configuration():
    files = [ROOT / name for name in ('CMakeLists.txt', 'opentlv-scm-1.rockspec', '.clang-format',
                                     '.clang-tidy', '.pre-commit-config.yaml', 'requirements-dev.txt')]
    for directory in ('.github/workflows', 'scripts', 'cmake', 'tests', 'src', 'lua'):
        files += [p for p in (ROOT / directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(files)}


def check_results(results, tier):
    expected = {'build', 'quality', 'codeql'} if tier != 'main' else {'smoke'}
    require(set(results) == expected, 'Missing or unexpected required jobs')
    require(all(v.get('result') == 'success' for v in results.values()), 'Required Lua checks did not all succeed')


def validate_record(record, row, release=False, core_lock=None, core_release=None):
    same_identity(record)
    require(record.get('status') == 'passed' and record.get('schema') == 1, 'Failed or invalid Lua record')
    require(record.get('id') == row['id'] and record.get('lua_sha') == identity()['commit'], 'Wrong Lua configuration')
    expected_system = {'ubuntu-24.04': 'Linux', 'windows-latest': 'Windows', 'macos-latest': 'Darwin'}[row['os']]
    require(record.get('system') == expected_system and record.get('architecture'), 'Wrong platform evidence')
    require(record.get('requested_runtime') == row['lua'] and record.get('shared') == row['shared'] and
            record.get('sanitizer') == row['sanitizer'] and record.get('configuration') == row['configuration'],
            'Wrong runtime/linkage/sanitizer configuration')
    runtime = record.get('runtime', '')
    require(('LuaJIT' in runtime) if row['lua'] == 'luajit' else runtime == 'Lua ' + row['lua'], 'Wrong Lua runtime')
    require(record.get('tests') == sorted(EXPECTED_TESTS) and
            record.get('ctest_passed') == record.get('ctest_total') == len(EXPECTED_TESTS) and
            record.get('busted_passed', 0) > 0 and
            all(record.get(k) == 0 for k in ('busted_failed', 'busted_errors', 'busted_pending')) and
            min(record.get('query_fixtures', 0), record.get('query_checks', 0)) > 0,
            'Incomplete Lua suite evidence')
    require(not row['rocks'] or record.get('luarocks') is True, 'Missing LuaRocks verification')
    require(not row['package'] or record.get('package', {}).get('id') == row['package'], 'Missing module package')
    require(not row['package'] or record.get('dependency_checks') == SCENARIOS, 'Incomplete dependency checks')
    if release:
        dep = record.get('core', {})
        require(dep.get('verified') is True and dep.get('mode') in ('verified-source', 'verified-binary') and
                dep.get('commit') == core_lock['commit'] == record.get('core_sha') and
                dep.get('release_tag') == core_lock['release_tag'] and
                all(dep.get(k) == v for k, v in core_release.items()), 'Unverified or replaced Core dependency')


def validate_inventory(records, tier, **kwargs):
    rows = matrix(tier)
    require(len(records) == len(rows) and {r.get('id') for r in records} == {r['id'] for r in rows},
            'Incomplete or duplicate Lua configuration inventory')
    by_id = {r['id']: r for r in records}
    for row in rows:
        validate_record(by_id[row['id']], row, **kwargs)


def gate(tier, results, records):
    check_results(results, tier)
    if tier == 'main':
        require(len(records) == 1, 'Missing Main smoke record')
        same_identity(records[0])
        require(records[0].get('smoke') is True and records[0].get('status') == 'passed', 'Main smoke failed')
    else:
        validate_inventory(records, tier)
    return {'schema': 1, **identity(), 'tier': tier, 'configuration': configuration(),
            'checks': results, 'records': records}
