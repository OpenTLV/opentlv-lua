# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Regressions for delayed Actions job metadata observed in PRs #20 and #41."""

from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import ci_timings


def job(name, **changes):
    return {'name': name, 'status': 'completed', 'conclusion': 'success',
            'started_at': '2026-10-19T16:18:00Z', 'completed_at': '2026-10-19T16:19:00Z',
            **changes}


class Timings(unittest.TestCase):
    def test_run_scope_retries_incomplete_gate_metadata(self):
        report, api, sleep = self.measure([
            {'jobs': [job('smoke'), job('Lua quality gate', completed_at=None)]},
            {'jobs': [job('smoke'), job('Lua quality gate')]}], 'run')
        self.assertEqual(report['job_count'], 2)
        self.assertEqual(api.call_count, 2)
        sleep.assert_called_once_with(ci_timings.METADATA_RETRY_SECONDS)

    def measure(self, responses, scope='lua'):
        with patch.object(ci_timings, 'api', side_effect=responses) as api, \
                patch.object(ci_timings.time, 'sleep') as sleep:
            report = ci_timings.measure('OpenTLV/opentlv-lua', '39284346365', '1', scope)
        self.assertTrue(all(call.args[0].startswith(
            'repos/OpenTLV/opentlv-lua/actions/runs/39284346365/attempts/1/jobs?')
            for call in api.call_args_list))
        return report, api, sleep

    def test_waits_for_completion_timestamps_from_reported_failure(self):
        jobs = [job('checks / ubuntu-24.04 / ON / Debug'), job('checks / Lua quality gate')]
        report, api, sleep = self.measure([
            {'jobs': [dict(item, completed_at=None) for item in jobs]}, {'jobs': jobs}])
        self.assertEqual(report['job_count'], 2)
        self.assertEqual(report['job_seconds'], 120)
        self.assertEqual(report['execution_span_seconds'], 60)
        self.assertEqual(api.call_count, 2)
        sleep.assert_called_once_with(ci_timings.METADATA_RETRY_SECONDS)

    def test_does_not_publish_partial_cli_times(self):
        build = job('checks / build', conclusion='failure')
        gate = job('checks / Lua quality gate', conclusion='failure')
        report, _, sleep = self.measure([
            {'jobs': [build, dict(gate, status='in_progress', conclusion=None, completed_at=None)]},
            {'jobs': [build, gate, job('checks / sanitizer', conclusion='skipped',
                                     started_at=None, completed_at=None)]}])
        self.assertEqual(report['job_count'], 2)
        self.assertTrue(all(item['conclusion'] == 'failure' for item in report['jobs']))
        sleep.assert_called_once_with(ci_timings.METADATA_RETRY_SECONDS)

    def test_waits_for_gate_to_appear(self):
        report, _, sleep = self.measure([
            {'jobs': []}, {'jobs': [job('checks / Lua quality gate')]}])
        self.assertEqual(report['job_count'], 1)
        sleep.assert_called_once_with(ci_timings.METADATA_RETRY_SECONDS)

    def test_pr41_metadata_can_arrive_after_the_previous_retry_window(self):
        jobs = [job('checks / build / ubuntu-24.04-5.4-OFF'),
                job('checks / Lua quality gate')]
        for delayed in (jobs[:1], [jobs[0], dict(jobs[1], completed_at=None)]):
            with self.subTest(delayed=delayed):
                report, api, sleep = self.measure(
                    [{'jobs': delayed}] * 6 + [{'jobs': jobs}])
                self.assertEqual(report['job_count'], 2)
                self.assertEqual(report['job_seconds'], 120)
                self.assertEqual(api.call_count, 7)
                self.assertEqual(sleep.call_count, 6)

    def test_nested_full_scope_excludes_parent_release_and_timings(self):
        report, _, sleep = self.measure([{'jobs': [
            job('candidate'), job('full / checks / build'), job('full / checks / Lua quality gate'),
            job('full / Lua full timings', status='in_progress', conclusion=None, completed_at=None),
            job('binaries'), job('publish')]}])
        self.assertEqual([item['name'] for item in report['jobs']],
                         ['full / checks / build', 'full / checks / Lua quality gate'])
        sleep.assert_not_called()

    def test_run_scope_does_not_wait_for_its_own_running_job(self):
        report, _, sleep = self.measure([{'jobs': [job('smoke'), job('Lua quality gate'),
            job('Lua main timings', status='in_progress', conclusion=None, completed_at=None)]}], 'run')
        self.assertEqual(report['job_count'], 2)
        sleep.assert_not_called()

    def test_permanently_missing_data_fails_after_bounded_retries(self):
        with patch.object(ci_timings, 'api', return_value={'jobs': [
                job('checks / Lua quality gate', completed_at=None)]}) as api, \
                patch.object(ci_timings.time, 'sleep') as sleep:
            with self.assertRaisesRegex(ValueError, 'incomplete after 18 attempts.*checks / Lua quality gate'):
                ci_timings.measure('OpenTLV/opentlv-lua', '39284346365', '1', 'lua')
        self.assertEqual(api.call_count, ci_timings.METADATA_ATTEMPTS)
        self.assertEqual(sleep.call_count, ci_timings.METADATA_ATTEMPTS - 1)

    def test_missing_gate_diagnostic_includes_run_attempt_and_available_jobs(self):
        with patch.object(ci_timings, 'api', return_value={'jobs': [job('checks / build')]}), \
                patch.object(ci_timings.time, 'sleep'), patch('builtins.print') as output:
            with self.assertRaisesRegex(ValueError, 'No jobs selected') as error:
                ci_timings.measure('OpenTLV/opentlv-lua', '40516841136', '1', 'lua')
        self.assertIn('run 40516841136 attempt 1 scope lua', str(error.exception))
        self.assertIn('checks / build', str(error.exception))
        self.assertIn('No jobs selected', output.call_args.args[0])

    def test_all_skipped_jobs_do_not_produce_a_timing_report(self):
        with patch.object(ci_timings, 'api', return_value={'jobs': [
                job('checks / Lua quality gate', conclusion='skipped')]}), \
                patch.object(ci_timings.time, 'sleep'):
            with self.assertRaisesRegex(ValueError, 'No non-skipped jobs'):
                ci_timings.measure('OpenTLV/opentlv-lua', '40516841136', '1', 'lua')

    def test_pagination_keeps_all_jobs_in_scope(self):
        report, api, sleep = self.measure([
            {'jobs': [job(f'checks / build {i}') for i in range(100)]},
            {'jobs': [job('checks / Lua quality gate')]}])
        self.assertEqual(report['job_count'], 101)
        self.assertTrue(api.call_args.args[0].endswith('&page=2'))
        sleep.assert_not_called()


if __name__ == '__main__':
    unittest.main()
