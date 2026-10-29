# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Release prose must be reviewable, deterministic and limited to its evidence."""

from copy import deepcopy
import hashlib
from pathlib import Path
import re
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import release_notes as notes
from ci_support import read

FIXTURE = notes.ROOT / 'tests/fixtures/release-notes-v0.2.0'


class ReleaseNotes(unittest.TestCase):
    def setUp(self):
        self.manifest = read(FIXTURE / 'manifest.json')
        self.changelog = (FIXTURE / 'CHANGELOG.md').read_text(encoding='utf-8')
        self.editorial = read(FIXTURE / 'editorial.json')

    def render(self):
        return notes.render(self.manifest, self.changelog, self.editorial, historical=True)

    def test_historical_example_is_exact_and_deterministic(self):
        self.assertEqual(hashlib.sha256((FIXTURE / 'manifest.json').read_bytes().replace(b'\r\n', b'\n')).hexdigest(),
                         '747696b13da60b183da4af51f2198a6941a800ac4ed17a80c5cfd97efd4210f7')
        expected = (notes.ROOT / 'docs/release-notes-v0.2.0-example.md').read_text(encoding='utf-8')
        self.assertEqual(self.render(), expected)
        self.manifest['quality']['records'].reverse()
        self.assertEqual(self.render(), expected)
        self.assertEqual(re.findall(r'^## (.+)$', expected, re.MULTILINE), list(notes.SECTIONS))
        self.assertNotIn('# OpenTLV Lua', expected)
        self.assertIn('77abc807bdd59956790497483a9d1fbad4b79019', expected)
        self.assertIn('/actions/runs/40522912843/attempts/1', expected)

    def test_reviewed_candidate_preview_is_current(self):
        editorial = read(notes.ROOT / 'docs/release-notes.json')
        notes.reviewed(notes.ROOT, 'v' + editorial['version'])

    def test_stale_preview_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'docs').mkdir()
            (root / 'cmake').mkdir()
            for name in ('CHANGELOG.md', 'docs/release-notes.json', 'docs/release-preview.md', 'cmake/core-artifact.json'):
                (root / name).write_bytes((notes.ROOT / name).read_bytes())
            tag = 'v' + read(root / 'docs/release-notes.json')['version']
            notes.reviewed(root, tag)
            preview = root / 'docs/release-preview.md'
            preview.write_bytes(preview.read_text(encoding='utf-8').replace('\n', '\r\n').encode('utf-8'))
            notes.reviewed(root, tag)  # Git's Windows line-ending conversion does not alter the reviewed prose.
            with (root / 'docs/release-preview.md').open('a', encoding='utf-8') as stream:
                stream.write('Extra unreviewed text.\n')
            with self.assertRaisesRegex(ValueError, 'preview is stale'):
                notes.reviewed(root, tag)

    def test_editorial_overview_is_literal_and_does_not_repeat_title(self):
        self.editorial['overview'] = 'Adds <widgets> & [labels] | `values` *safely*.'
        body = self.render()
        self.assertIn(r'Adds &lt;widgets&gt; &amp; \[labels\] \| \`values\` \*safely\*\.', body)
        for text in ('OpenTLV Lua 0.2.0', 'OpenTLV Lua v0.2.0 adds features', 'A line\n## Forged', '', None):
            self.editorial['overview'] = text
            with self.subTest(text=text), self.assertRaises(ValueError):
                self.render()

    def test_wrong_editorial_version_rejected(self):
        self.editorial['version'] = '0.3.0'
        with self.assertRaisesRegex(ValueError, 'Editorial overview'):
            self.render()

    def test_issue_links_preserve_ownership_without_author_names(self):
        before = self.changelog
        body = self.render()
        self.assertIn('([#40](https://github.com/OpenTLV/opentlv-lua/issues/40))', body)
        self.assertIn('(https://github.com/OpenTLV/opentlv-docs/issues/42)', body)
        self.assertNotIn('by [@', body)
        self.assertNotIn('## Contributors', body)
        self.assertEqual(self.changelog, before)

    def test_changelog_categories_formatting_and_links(self):
        text = '# Changes\n\n## [0.2.0] - 2026-10-27\n\n### Fixed\n\n- Keep `code`.\n- Literal `(#42)`\n\n### Added\n\n' \
               '- A [guide](https://example.com/guide).\n  Continuation.\n\n### Removed\n\n'
        output = notes.changes(text, 'v0.2.0')
        self.assertLess(output.index('### Added'), output.index('### Fixed'))
        self.assertIn('- Keep `code`.', output)
        self.assertIn('- Literal `(#42)`', output)
        self.assertIn('  Continuation.', output)
        self.assertNotIn('### Removed', output)
        for bad in (text.replace('https://example.com/guide', 'docs/api.md'),
                    text.replace('### Fixed', '### Unknown'), text + '### Fixed\n\n- Duplicate.\n',
                    text.replace('[0.2.0]', '[v0.2.0]'), text + text):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                notes.changes(bad, 'v0.2.0')

    def test_registry_and_unavailable_artifacts_not_promised(self):
        body = self.render()
        distribution = body.split('## Installation / Distribution\n', 1)[1].split('## Verification', 1)[0]
        self.assertNotIn('luarocks install', distribution)
        self.assertNotIn('.src.rock', distribution)
        self.assertNotIn('.rockspec', distribution)
        self.assertNotIn('macos.zip', distribution)
        self.assertIn('separate approved operation', distribution)
        self.assertIn('Lua 5.4', distribution)
        self.assertNotIn('/release-notes.md)', body)  # The historical release did not ship this asset.

    def test_missing_or_failed_evidence_rejected(self):
        original = deepcopy(self.manifest)
        changes = [lambda m: m.update(schema=2),
                   lambda m: m['request'].update(tag='0.2.0'),
                   lambda m: m['request'].update(version='v0.2.0'),
                   lambda m: m['request'].update(commit='main'),
                   lambda m: m['quality'].update(tier='main'),
                   lambda m: m['quality'].update(run_attempt='2'),
                   lambda m: m['quality']['checks']['codeql'].update(result='skipped'),
                   lambda m: m['quality']['records'].pop(),
                   lambda m: m['quality']['records'][0].update(status='failed'),
                   lambda m: m['quality']['records'][0].update(ctest_passed=0),
                   lambda m: m['quality']['records'][0].update(query_checks=0),
                   lambda m: m['quality']['records'][0].update(run_id='1'),
                   lambda m: m['quality']['records'][0]['core'].update(verified=False),
                   lambda m: m['quality']['records'][0]['core'].update(commit='b' * 40),
                   lambda m: m['request']['core_release'].update(manifest_sha256='invalid'),
                   lambda m: next(r for r in m['quality']['records'] if r.get('package'))['package'].update(
                       filename='../escape.zip'),
                   lambda m: next(r for r in m['quality']['records'] if r.get('package'))['package'].update(
                       sha256='invalid'),
                   lambda m: next(r for r in m['quality']['records'] if r.get('package')).update(architecture='arm64')]
        for index, change in enumerate(changes):
            self.manifest = deepcopy(original)
            change(self.manifest)
            with self.subTest(mutation=index), self.assertRaises(ValueError):
                self.render()

    def test_current_renderer_requires_complete_release_rock_evidence(self):
        normalized = self.changelog.replace('## [v0.2.0]', '## [0.2.0]')
        with self.assertRaisesRegex(ValueError, 'release rock evidence'):
            notes.render(self.manifest, normalized, self.editorial)
        request = self.manifest['request']
        rock = {'tag': 'v0.2.0', 'commit': request['commit'], 'core_commit': request['core_lock']['commit'],
                'rock': 'opentlv-0.2.0-1.src.rock', 'sha256': 'a' * 64, 'rockspec_sha256': 'b' * 64,
                'tests': list(notes.PACKAGE_TESTS), 'query': True}
        for r in self.manifest['quality']['records']:
            if r.get('luarocks') and r['shared'] == 'OFF':
                r['release_rock'] = deepcopy(rock)
        body = notes.render(self.manifest, normalized, self.editorial)
        preview = notes.preview(normalized, self.editorial, request['core_lock'], request['tag'])
        self.assertEqual(body.split('## Compatibility', 1)[0], preview.split('## Compatibility', 1)[0])
        self.assertIn('/opentlv-0.2.0-1.src.rock)', body)
        self.assertIn('/release-notes.md)', body)
        self.assertNotIn('luarocks install', body)
        next(r for r in self.manifest['quality']['records'] if r.get('release_rock'))['release_rock']['sha256'] = 'c' * 64
        with self.assertRaisesRegex(ValueError, 'Inconsistent release rock evidence'):
            notes.render(self.manifest, normalized, self.editorial)


if __name__ == '__main__':
    unittest.main()
