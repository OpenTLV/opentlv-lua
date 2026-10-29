#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Check versioned rockspec generation without reading the real release tags."""

import difflib
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from rockspec import development, versioned  # noqa: E402


class RockspecTests(unittest.TestCase):
    def setUp(self):
        self.scm = development()

    def test_release_changes_only_version_source_tag_and_release_definition(self):
        name, text = versioned(self.scm, 'v1.2.3')
        self.assertEqual(name, 'opentlv-1.2.3-1.rockspec')
        changes = [line for line in difflib.unified_diff(self.scm.splitlines(), text.splitlines(), lineterm='', n=0)
                   if line[:1] in '+-' and not line.startswith(('+++', '---'))]
        self.assertEqual([line for line in changes if line.startswith('-')], [
            '-version = "scm-1"',
            '-   url = "git+https://github.com/OpenTLV/opentlv-lua.git"',
            "-      '\"-DOPENTLV_LUA_RELEASE_TAG=\" && ' ..",
        ])
        self.assertEqual([line for line in changes if line.startswith('+')], [
            '+version = "1.2.3-1"',
            '+   url = "git+https://github.com/OpenTLV/opentlv-lua.git",',
            '+   tag = "v1.2.3"',
            "+      '\"-DOPENTLV_LUA_RELEASE_TAG=v1.2.3\" && ' ..",
        ])
        self.assertRegex(text, r'(?m)^package = "opentlv"$')
        self.assertRegex(text, r'(?m)^\s*license = "MIT"$')

    def test_packaging_revision_is_not_part_of_the_tag(self):
        name, text = versioned(self.scm, 'v0.3.0', revision=2)
        self.assertEqual(name, 'opentlv-0.3.0-2.rockspec')
        self.assertIn('\nversion = "0.3.0-2"\n', text)
        self.assertIn('   tag = "v0.3.0"\n', text)
        self.assertIn('"-DOPENTLV_LUA_RELEASE_TAG=v0.3.0"', text)

    def test_invalid_release_rejected(self):
        for tag in ('0.1.0', 'v01.2.3', 'v1.02.3', 'v1.2.03', 'v1.2', 'v1.2.3-rc.1', 'v1.2.3+build', 'scm'):
            with self.subTest(tag=tag), self.assertRaisesRegex(ValueError, 'vMAJOR.MINOR.PATCH'):
                versioned(self.scm, tag)
        with self.assertRaisesRegex(ValueError, 'revision starts at 1'):
            versioned(self.scm, 'v1.2.3', revision=0)

    def test_source_without_release_definition_rejected(self):
        # Like v0.1.0 and v0.2.0: a LuaRocks build of such a tag reports scm+unknown.
        older = self.scm.replace('"-DOPENTLV_LUA_RELEASE_TAG="', '')
        self.assertNotEqual(older, self.scm)
        with self.assertRaisesRegex(ValueError, 'cannot report their release version'):
            versioned(older, 'v0.2.0')

    def test_already_versioned_source_rejected(self):
        _, release = versioned(self.scm, 'v1.2.3')
        with self.assertRaisesRegex(ValueError, 'version "scm-1"'):
            versioned(release, 'v1.2.4')


if __name__ == '__main__':
    unittest.main()
