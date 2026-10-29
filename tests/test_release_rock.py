#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Check the release source rock layout; LuaRocks installation runs in Full CI."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_release_rock import SOURCE_DIR, TIMESTAMP, git, source_rock  # noqa: E402
from ci_support import digest  # noqa: E402
from rockspec import DEVELOPMENT, versioned  # noqa: E402


class ReleaseRockTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='lua release rock ')
        self.addCleanup(temp.cleanup)
        self.temp = Path(temp.name)
        self.commit = git('rev-parse', 'HEAD').decode().strip()

    def fixture(self, files, link=None):
        """Create a repository with the real development rockspec and files."""
        root = self.temp / 'fixture'
        root.mkdir()
        run = lambda *args: subprocess.run(['git', '-C', str(root), *args], check=True, capture_output=True)  # noqa: E731
        run('init')
        for name, data in {DEVELOPMENT: git('show', f'{self.commit}:{DEVELOPMENT}'), **files}.items():
            (root / name).write_bytes(data)
        run('add', '.')
        if link:
            # A symbolic link without a checkout that supports one.
            blob = subprocess.run(['git', '-C', str(root), 'hash-object', '-w', '--stdin'], input=b'target',
                                  check=True, capture_output=True).stdout.decode().strip()
            run('update-index', '--add', '--cacheinfo', f'120000,{blob},{link}')
        run('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', '-c', 'commit.gpgsign=false',
            '-c', 'core.hooksPath=', 'commit', '-m', 'fixture')
        return root, run('rev-parse', 'HEAD').stdout.decode().strip()

    def test_rock_holds_rockspec_and_exact_commit_tree(self):
        out = self.temp / 'out'
        out.mkdir()
        rockspec, rock = source_rock(out, self.commit, 'v1.2.3')
        self.assertEqual((rockspec.name, rock.name), ('opentlv-1.2.3-1.rockspec', 'opentlv-1.2.3-1.src.rock'))
        expected = versioned(git('show', f'{self.commit}:{DEVELOPMENT}').decode('utf-8'), 'v1.2.3')[1]
        self.assertEqual(rockspec.read_bytes(), expected.encode('utf-8'))
        tracked = [p for p in git('ls-tree', '-r', '-z', '--name-only', self.commit).decode().split('\0') if p]
        with zipfile.ZipFile(rock) as archive:
            entries = archive.infolist()
            self.assertEqual([e.filename for e in entries],
                             [rockspec.name] + [f'{SOURCE_DIR}/{p}' for p in tracked if p != '.gitignore'])
            self.assertTrue(all(e.compress_type == zipfile.ZIP_STORED and e.date_time == TIMESTAMP for e in entries))
            self.assertEqual(archive.read(rockspec.name), rockspec.read_bytes())
            for name in ('LICENSE', 'opentlv-scm-1.rockspec', 'lua/opentlv/init.lua', 'cmake/Core.cmake'):
                self.assertEqual(archive.read(f'{SOURCE_DIR}/{name}'), git('show', f'{self.commit}:{name}'))

    def test_rock_bytes_are_reproducible(self):
        digests = []
        for name in ('first', 'second'):
            out = self.temp / name
            out.mkdir()
            digests.append(tuple(digest(path) for path in source_rock(out, self.commit, 'v0.0.0')))
        self.assertEqual(digests[0], digests[1])

    def test_links_and_export_attributes_rejected(self):
        root, commit = self.fixture({'README.md': b'readme\n'}, link='linked')
        with self.assertRaisesRegex(ValueError, 'regular files'):
            source_rock(self.temp, commit, 'v1.2.3', root)
        (self.temp / 'fixture').rename(self.temp / 'linked')
        root, commit = self.fixture({'.gitattributes': b'docs export-ignore\n'})
        with self.assertRaisesRegex(ValueError, 'Export attributes'):
            source_rock(self.temp, commit, 'v1.2.3', root)

    def test_invalid_tag_rejected(self):
        with self.assertRaisesRegex(ValueError, 'vMAJOR.MINOR.PATCH'):
            source_rock(self.temp, self.commit, '1.2.3')


if __name__ == '__main__':
    unittest.main()
