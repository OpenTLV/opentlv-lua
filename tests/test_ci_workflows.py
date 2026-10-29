# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Protect workflow routing and release authorization boundaries."""

from pathlib import Path
import unittest
import yaml

ROOT = Path(__file__).resolve().parents[1]


def workflow(name):
    return yaml.load((ROOT / '.github/workflows' / name).read_text(), Loader=yaml.BaseLoader)


class Workflows(unittest.TestCase):
    def test_four_entry_points(self):
        for name, events in [('lua-pr.yml', {'pull_request'}), ('lua-main.yml', {'push'}),
                             ('lua-full.yml', {'workflow_call', 'workflow_dispatch'}),
                             ('release.yml', {'workflow_dispatch'})]:
            with self.subTest(name=name):
                self.assertEqual(set(workflow(name)['on']), events)
        self.assertNotIn('paths', workflow('lua-pr.yml')['on']['pull_request'])
        self.assertEqual(set(workflow('release.yml')['on']['workflow_dispatch']['inputs']), {'tag'})

    def test_workers_are_reusable_only(self):
        for name in ('lua.yml', 'lua-checks.yml', 'quality.yml', 'lua-codeql.yml'):
            self.assertEqual(set(workflow(name)['on']), {'workflow_call'})

    def test_always_gate_and_release_dependencies(self):
        gate = workflow('lua-checks.yml')['jobs']['gate']
        self.assertEqual(gate['if'], 'always()')
        self.assertEqual(set(gate['needs']), {'build', 'quality', 'codeql', 'api_coverage'})
        jobs = workflow('release.yml')['jobs']
        self.assertEqual(jobs['full-ci']['uses'], './.github/workflows/lua-full.yml')
        self.assertEqual(set(jobs['publish']['needs']), {'full-ci', 'assemble'})
        self.assertEqual(jobs['publish']['permissions']['contents'], 'write')
        self.assertNotIn('contents: write', (ROOT / '.github/workflows/lua.yml').read_text())

    def test_artifacts_are_attempt_scoped_and_missing_files_fail(self):
        for path in (ROOT / '.github/workflows').glob('*.yml'):
            doc = workflow(path.name)
            for job in doc['jobs'].values():
                for step in job.get('steps', []):
                    if step.get('uses', '').startswith('actions/upload-artifact@'):
                        self.assertIn('github.run_attempt', step['with']['name'])
                        self.assertEqual(step['with']['if-no-files-found'], 'error')

    def test_luarocks_upload_needs_approval_and_holds_the_only_credential(self):
        doc = workflow('luarocks-release.yml')
        self.assertEqual(set(doc['on']), {'workflow_dispatch'})
        self.assertEqual(set(doc['on']['workflow_dispatch']['inputs']), {'tag'})
        self.assertEqual(doc['permissions'], {'contents': 'read'})
        self.assertEqual(doc['concurrency']['cancel-in-progress'], 'false')
        verify, publish = doc['jobs']['verify'], doc['jobs']['publish']
        self.assertEqual(set(doc['jobs']), {'verify', 'publish'})
        self.assertNotIn('environment', verify)
        self.assertEqual(publish['environment'], 'luarocks')
        self.assertEqual(publish['needs'], 'verify')
        for job in (verify, publish):
            self.assertEqual(job['permissions'], {'contents': 'read', 'actions': 'read', 'attestations': 'read'})
            for step in job['steps']:
                if step.get('uses', '').startswith('actions/checkout@'):
                    self.assertEqual(step['with']['persist-credentials'], 'false')
        self.assertNotIn('secrets.', str(verify))
        self.assertEqual([s['env']['LUAROCKS_API_KEY'] for s in publish['steps'] if 'LUAROCKS_API_KEY' in s.get('env', {})],
                         ['${{ secrets.LUAROCKS_API_KEY }}'])
        for path in (ROOT / '.github/workflows').glob('*.yml'):
            if path.name != 'luarocks-release.yml':
                self.assertNotIn('LUAROCKS_API_KEY', path.read_text(), path.name)


if __name__ == '__main__':
    unittest.main()
