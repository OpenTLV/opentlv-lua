# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Reject incomplete, stale and cross-configuration release evidence."""

from copy import deepcopy
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import lua_policy as policy
import release
from ci_support import identity
from lua_package import PACKAGE_TESTS
from lua_suite import EXPECTED_TESTS, ctest_evidence

ENV = {'GITHUB_REPOSITORY': 'OpenTLV/opentlv-lua', 'GITHUB_SHA': 'a' * 40,
       'GITHUB_RUN_ID': '123', 'GITHUB_RUN_ATTEMPT': '1'}
LOCK = {'commit': 'b' * 40, 'release_tag': 'v0.8.0'}
CORE_RELEASE = {'release_id': 77, 'manifest_sha256': 'c' * 64}


def release_rock(tag='v0.2.0'):
    return {'tag': tag, 'commit': ENV['GITHUB_SHA'], 'rock': f'opentlv-{tag[1:]}-1.src.rock', 'sha256': 'e' * 64,
            'rockspec_sha256': 'f' * 64, 'core_commit': LOCK['commit'], 'tests': list(PACKAGE_TESTS), 'query': True,
            'metadata': {'binding_version': tag[1:], 'binding_revision': '', 'release_tag': tag, 'dirty': False}}


def records(tag='v0.2.0'):
    return [dict(schema=1, **identity(), id=row['id'], lua_sha=ENV['GITHUB_SHA'], status='passed',
                 system={'ubuntu-24.04': 'Linux', 'windows-latest': 'Windows', 'macos-latest': 'Darwin'}[row['os']],
                 architecture='x86_64',
                 core_sha=LOCK['commit'], core=dict(verified=True, mode='verified-source', **LOCK, **CORE_RELEASE),
                 runtime='LuaJIT 2.1' if row['lua'] == 'luajit' else 'Lua ' + row['lua'],
                 requested_runtime=row['lua'], shared=row['shared'], sanitizer=row['sanitizer'],
                 configuration=row['configuration'], tests=sorted(EXPECTED_TESTS),
                 ctest_passed=len(EXPECTED_TESTS), ctest_total=len(EXPECTED_TESTS),
                 busted_passed=79, busted_failed=0, busted_errors=0,
                 busted_pending=0, query_fixtures=171, query_checks=3422, luarocks=row['rocks'],
                 dependency_checks=policy.SCENARIOS if row['package'] else [],
                 package={'id': row['package']} if row['package'] else None,
                 release_rock=release_rock(tag) if row['rocks'] and row['shared'] == 'OFF' else None)
            for row in policy.matrix('full')]


class Gates(unittest.TestCase):
    def setUp(self):
        context = patch.dict(os.environ, ENV)
        context.start()
        self.addCleanup(context.stop)

    def validate(self, value):
        policy.validate_inventory(value, 'full', release=True, core_lock=LOCK, core_release=CORE_RELEASE)

    def test_full_inventory(self):
        self.validate(records())
        self.assertEqual(len(records()), 17)

    def test_each_configuration_required_once(self):
        for index in range(len(records())):
            for duplicate in (False, True):
                value = records()
                if duplicate:
                    value.append(deepcopy(value[index]))
                else:
                    value.pop(index)
                with self.subTest(index=index, duplicate=duplicate), self.assertRaises(ValueError):
                    self.validate(value)

    def test_wrong_identity_and_skips_rejected(self):
        mutations = {'status': 'skipped', 'system': 'Windows', 'architecture': '',
                     'repository': 'other/repo', 'commit': 'd' * 40,
                     'lua_sha': 'd' * 40, 'run_id': '124', 'run_attempt': '2', 'runtime': 'Lua 5.2',
                     'shared': 'ON', 'sanitizer': True, 'configuration': 'Debug', 'ctest_total': 14,
                     'ctest_passed': 14, 'busted_passed': 0, 'busted_pending': 1, 'busted_errors': 1,
                     'query_fixtures': 0, 'query_checks': 0, 'tests': [], 'luarocks': False}
        for key, value in mutations.items():
            data = records()
            data[0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.validate(data)

    def test_dependency_mismatch_and_unverified_source_rejected(self):
        for key, value in {'verified': False, 'mode': 'pinned-git', 'commit': 'd' * 40,
                           'release_tag': 'v0.7.1', 'release_id': 78, 'manifest_sha256': 'd' * 64}.items():
            data = records()
            data[0]['core'][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.validate(data)

    def test_release_rock_required_on_every_static_rocks_configuration(self):
        static = [i for i, r in enumerate(records()) if r['release_rock']]
        self.assertEqual(len(static), 13)
        for index in static:
            data = records()
            data[index]['release_rock'] = None
            with self.subTest(index=index), self.assertRaises(ValueError):
                self.validate(data)
        mutations = {'tag': '0.2.0', 'commit': 'd' * 40, 'core_commit': 'd' * 40, 'rock': 'opentlv-0.2.0-2.src.rock',
                     'sha256': 'invalid', 'rockspec_sha256': '', 'tests': [], 'query': False,
                     'metadata': {'binding_version': '0.2.0', 'binding_revision': 'a' * 40,
                                  'release_tag': 'v0.2.0', 'dirty': False}}
        for key, value in mutations.items():
            data = records()
            data[static[0]]['release_rock'][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.validate(data)

    def test_release_rock_bytes_identical_across_configurations(self):
        for key in ('sha256', 'rockspec_sha256'):
            data = records()
            next(r for r in data if r['release_rock'])['release_rock'][key] = '9' * 64
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'differ between configurations'):
                self.validate(data)

    def test_each_top_level_job_required(self):
        good = {key: {'result': 'success'} for key in ('build', 'quality', 'codeql', 'api_coverage')}
        policy.check_results(good, 'full')
        for key in good:
            for result in ('failure', 'cancelled', 'skipped', 'missing'):
                changed = deepcopy(good)
                if result == 'missing':
                    del changed[key]
                else:
                    changed[key]['result'] = result
                with self.subTest(key=key, result=result), self.assertRaises(ValueError):
                    policy.check_results(changed, 'full')

    def test_main_requires_real_smoke(self):
        result = {**identity(), 'smoke': True, 'status': 'passed'}
        with patch.object(policy, 'configuration', return_value={}):
            policy.gate('main', {'smoke': {'result': 'success'}}, [result])
            for value in ([], [result, result], [result | {'smoke': False}], [result | {'run_attempt': '2'}]):
                with self.assertRaises(ValueError):
                    policy.gate('main', {'smoke': {'result': 'success'}}, value)

    def test_full_cannot_be_replaced_by_pr_or_changed_definition(self):
        request = {'core_lock': LOCK, 'core_release': CORE_RELEASE, 'tag': 'v0.2.0'}
        quality = dict(schema=1, **identity(), tier='full', configuration={'known': 'hash'}, records=records(),
                       checks={key: {'result': 'success'} for key in ('build', 'quality', 'codeql', 'api_coverage')})
        with patch.object(release, 'configuration', return_value={'known': 'hash'}):
            release.check_quality(quality, request)
            for changes in ({'tier': 'pr'}, {'configuration': {}}, {'run_attempt': '2'},
                            {'records': records('v0.0.0')}):
                with self.assertRaises(ValueError):
                    release.check_quality(quality | changes, request)

    def test_ctest_summary_formats_and_contradictions(self):
        for summary in ('100% tests passed out of 15', '100% tests passed, 0 tests failed out of 15'):
            evidence = {}
            ctest_evidence(summary + '\n78 successes / 0 failures / 0 errors / 0 pending\n', evidence)
            self.assertEqual(evidence['ctest_passed'], 15)
            self.assertEqual(evidence['busted_passed'], 78)
        evidence = {}
        ctest_evidence('100% tests passed, 1 tests failed out of 15\n', evidence)
        self.assertNotEqual(evidence.get('ctest_passed'), 15)


if __name__ == '__main__':
    unittest.main()
