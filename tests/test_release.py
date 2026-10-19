# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Release entry points reject invalid requests and never mutate on failed gates."""

from copy import deepcopy
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import release
import release_support as support
from lua_package import PACKAGE_TESTS
from ci_support import digest, write


class Release(unittest.TestCase):
    def test_request_requires_main_manual_canonical_workflow(self):
        sha = 'a' * 40
        env = {'GITHUB_REPOSITORY': support.REPOSITORY, 'GITHUB_EVENT_NAME': 'workflow_dispatch',
               'GITHUB_REF': 'refs/heads/main', 'GITHUB_SHA': sha,
               'GITHUB_WORKFLOW_REF': support.REPOSITORY + '/.github/workflows/release.yml@refs/heads/main',
               'GITHUB_RUN_ID': '123', 'GITHUB_RUN_ATTEMPT': '1'}

        def git(*args, **kwargs):
            if args[:2] == ('remote', 'get-url'):
                return 'https://github.com/' + support.REPOSITORY + '.git'
            if args[0] == 'show':
                return 'GIT_REPOSITORY https://github.com/OpenTLV/opentlv.git\nGIT_TAG ' + 'b' * 40
            return sha

        with patch.dict(os.environ, env), patch.object(support, 'git', side_effect=git), \
                patch.object(support.subprocess, 'run'):
            self.assertEqual(support.request_identity('0.2.0', sha)['tag'], 'v0.2.0')
            for key, value in {'GITHUB_REPOSITORY': 'other/lua', 'GITHUB_EVENT_NAME': 'push',
                               'GITHUB_REF': 'refs/heads/feature', 'GITHUB_WORKFLOW_REF': 'other',
                               'GITHUB_SHA': 'main', 'GITHUB_RUN_ATTEMPT': ''}.items():
                with self.subTest(key=key), patch.dict(os.environ, {key: value}), self.assertRaises(ValueError):
                    support.request_identity('0.2.0', sha)

    def test_closed_changelog(self):
        good = '# Changes\n\n## [v0.2.0] - 2026-10-19\n\n- New gates.\n'
        release.closed_changelog(good, 'v0.2.0')
        for text in (good + 'Unreleased', good.replace('v0.2.0', '0.2.0'),
                     good.replace('10-19', '02-30'), good.replace('- New gates.', ''), '# Changes'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                release.closed_changelog(text, 'v0.2.0')

    def test_invalid_tags_do_not_reach_remote(self):
        with patch.object(release, 'api') as api:
            for tag in ('0.2.0', 'v01.2.0', 'main', 'v0.2.0;cmd', ''):
                with self.assertRaises(ValueError):
                    release.preflight(tag)
            api.assert_not_called()

    def test_core_pin_is_single_canonical_immutable_sha(self):
        good = 'GIT_REPOSITORY https://github.com/OpenTLV/opentlv.git\nGIT_TAG ' + 'a' * 40
        self.assertEqual(support.core_pin(good + ' # comment'), 'a' * 40)
        for text in (good + '\nGIT_TAG ' + 'a' * 40, good.replace('a' * 40, 'main'),
                     good.replace('OpenTLV/opentlv', 'other/core')):
            with self.assertRaises(ValueError):
                support.core_pin(text)

    def test_existing_tag_or_wrong_core_blocks_request(self):
        identity = {'lua_sha': 'a' * 40, 'core_sha': 'b' * 40, 'tag': 'v0.2.0'}
        responses = [{'object': {'sha': 'a' * 40}},
                     {'status': 'identical', 'base_commit': {'sha': 'a' * 40}}, {'sha': 'b' * 40},
                     {'object': {'sha': 'a' * 40}}]
        with patch.object(support, 'api', side_effect=responses), self.assertRaisesRegex(ValueError, 'already exists'):
            support.verify_remote(identity)
        responses[2] = {'sha': 'c' * 40}
        with patch.object(support, 'api', side_effect=responses), self.assertRaisesRegex(ValueError, 'Pinned Core'):
            support.verify_remote(identity)

    def test_attempt_identity_and_cancelled_run(self):
        identity = {'run_id': '123', 'run_attempt': '1', 'workflow_sha': 'a' * 40}
        good = {'id': 123, 'run_attempt': 1, 'head_sha': 'a' * 40, 'event': 'workflow_dispatch',
                'head_branch': 'main', 'repository': {'full_name': support.REPOSITORY},
                'path': '.github/workflows/release.yml', 'status': 'in_progress', 'conclusion': None}
        for change in ({'run_attempt': 2}, {'head_sha': 'b' * 40}, {'event': 'push'},
                       {'status': 'completed', 'conclusion': 'cancelled'}):
            with patch.object(support, 'api', return_value=good | change), self.assertRaises(ValueError):
                support.attempt_jobs(identity)

    def test_failed_gate_or_attestation_never_creates_tag(self):
        for failure in ('attestation', 'job'):
            with patch.object(release, 'verify_payload', side_effect=ValueError('attestation') if failure == 'attestation'
                              else None, return_value={}), patch.object(release, 'api') as api, \
                    patch.dict(os.environ, {'FULL_CI_RESULT': 'failure', 'ASSEMBLE_RESULT': 'success'}):
                with self.assertRaises(ValueError):
                    release.publish(Path('unused'))
                api.assert_not_called()

    def test_replaced_preflight_lock_blocks_assembly(self):
        with patch.object(release, 'preflight', return_value={'tag': 'v0.2.0', 'changed': True}):
            with tempfile.TemporaryDirectory() as tmp, self.assertRaises(ValueError):
                release.assemble({'tag': 'v0.2.0'}, {}, Path(tmp), Path(tmp) / 'out')

    def test_package_identity_and_complete_relocated_tests(self):
        request = {'version': '0.2.0', 'tag': 'v0.2.0', 'commit': 'a' * 40}
        package = {'id': 'linux-x64-lua54', 'filename': 'module.zip', 'tests': list(PACKAGE_TESTS),
                   'query': True, 'sha256': 'b' * 64, 'metadata': {'binding_version': '0.2.0',
                   'release_tag': 'v0.2.0', 'binding_revision': 'a' * 40, 'dirty': False}}
        record = {'package': package, 'shared': 'OFF', 'requested_runtime': '5.4',
                  'architecture': 'x86_64', 'system': 'Linux',
                  'compiler': {'SIZEOF_DATA_PTR': '8'}, 'sanitizer': False, 'configuration': 'Release'}
        release.check_package(record, request)
        for key, value in {'tests': [], 'query': False, 'filename': '../escape', 'sha256': 'invalid'}.items():
            bad = deepcopy(record)
            bad['package'][key] = value
            with self.assertRaises(ValueError):
                release.check_package(bad, request)

    def test_modified_payload_rejected_before_attestation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            request = {'tag': 'v0.2.0'}
            packages = [{'package': {'id': identifier, 'filename': identifier + '.zip', 'sha256': 'a' * 64}}
                        for identifier in release.PACKAGES]
            write(root / 'release-manifest.json', {'schema': 1, 'request': request, 'quality': {'records': packages}})
            for record in packages:
                (root / record['package']['filename']).write_bytes(b'tampered')
            names = ['release-manifest.json'] + [r['package']['filename'] for r in packages]
            (root / 'SHA256SUMS').write_text(''.join(f'{digest(root / n)}  {n}\n' for n in names))
            with patch.object(release, 'preflight', return_value=request), patch.object(release, 'check_quality'), \
                    patch.object(release, 'check_package'), patch.object(release, 'run') as command:
                with self.assertRaisesRegex(ValueError, 'digest changed'):
                    release.verify_payload(root)
                command.assert_not_called()

    def test_assemble_verify_and_publish_complete_fixture(self):
        from test_ci_gates import ENV, LOCK, CORE_RELEASE, records
        from ci_support import identity
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, ENV):
            root = Path(tmp)
            request = dict(identity(), tag='v0.2.0', version='0.2.0', core_lock=LOCK,
                           core_release=CORE_RELEASE, lua_sha=ENV['GITHUB_SHA'], workflow_sha=ENV['GITHUB_SHA'])
            evidence = records()
            for record in evidence:
                if not record['package']:
                    continue
                package = record['package']
                archive = root / (package['id'] + '.zip')
                archive.write_bytes(b'controlled package fixture')
                record['compiler'] = {'SIZEOF_DATA_PTR': '8'}
                package.update(filename=archive.name, sha256=digest(archive), tests=list(PACKAGE_TESTS),
                               query=True, metadata={'binding_version': '0.2.0', 'release_tag': 'v0.2.0',
                                                     'binding_revision': request['commit'], 'dirty': False})
            quality = dict(identity(), schema=1, tier='full', configuration={'known': 'hash'}, records=evidence,
                           checks={key: {'result': 'success'} for key in ('build', 'quality', 'codeql')})
            with patch.object(release, 'preflight', return_value=request), \
                    patch.object(release, 'configuration', return_value={'known': 'hash'}), \
                    patch.object(release, 'run') as command:
                release.assemble(request, quality, root, root / 'release')
                self.assertEqual(release.verify_payload(root / 'release'), request)
                self.assertEqual(command.call_count, 4)
                jobs = [{'name': name, 'conclusion': 'success', 'run_attempt': 1, 'head_sha': request['commit']}
                        for name in ('Full / checks / Lua quality gate', 'Assemble and attest Lua release')]
                with patch.dict(os.environ, {'FULL_CI_RESULT': 'success', 'ASSEMBLE_RESULT': 'success'}), \
                        patch.object(release, 'attempt_jobs', return_value=jobs), \
                        patch.object(release, 'api', return_value={'object': {'sha': request['commit']}}) as api:
                    release.publish(root / 'release')
                    api.assert_called_once_with(release.REPOSITORY + '/git/refs',
                                                {'ref': 'refs/tags/v0.2.0', 'sha': request['commit']})
                    self.assertEqual(command.call_args.args[:3], ('gh', 'release', 'create'))

    def test_incomplete_same_attempt_jobs_never_publish(self):
        request = {'run_attempt': '1', 'commit': 'a' * 40}
        good = [{'name': name, 'conclusion': 'success', 'run_attempt': 1, 'head_sha': request['commit']}
                for name in ('Full / checks / Lua quality gate', 'Assemble and attest Lua release')]
        for field, value in (('conclusion', 'skipped'), ('run_attempt', 2), ('head_sha', 'b' * 40)):
            jobs = deepcopy(good)
            jobs[0][field] = value
            with patch.object(release, 'verify_payload', return_value=request), \
                    patch.object(release, 'attempt_jobs', return_value=jobs), patch.object(release, 'api') as api, \
                    patch.dict(os.environ, {'FULL_CI_RESULT': 'success', 'ASSEMBLE_RESULT': 'success'}):
                with self.assertRaises(ValueError):
                    release.publish(Path('fixture'))
                api.assert_not_called()


if __name__ == '__main__':
    unittest.main()
