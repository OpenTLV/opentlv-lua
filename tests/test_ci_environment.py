# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Keep module and native dependency lookup scoped to the tested build."""

import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from ci import lua_environment


class Environment(unittest.TestCase):
    def test_windows_dependency_path_preserves_tools_and_clears_lua_overrides(self):
        build = Path('selected-build')
        inherited = {'PATH': 'tools;system', 'LUA_CPATH_5_4': 'stale', 'LUA_INIT': 'stale'}
        with patch.dict(os.environ, inherited, clear=True), patch('os.name', 'nt'), patch('os.pathsep', ';'):
            env = lua_environment(build)
            self.assertEqual(os.environ['PATH'], inherited['PATH'])
        self.assertEqual(env['PATH'], str(build / 'opentlv') + ';tools;system')
        self.assertEqual(env['LUA_CPATH'], str(build / '?.dll') + ';;')
        self.assertNotIn('LUA_CPATH_5_4', env)
        self.assertNotIn('LUA_INIT', env)

    def test_posix_preserves_loader_environment(self):
        build = Path('selected-build')
        inherited = {'PATH': '/usr/bin', 'LD_LIBRARY_PATH': '/runtime'}
        with patch.dict(os.environ, inherited, clear=True), patch('os.name', 'posix'):
            env = lua_environment(build)
        self.assertEqual(env['PATH'], inherited['PATH'])
        self.assertEqual(env['LD_LIBRARY_PATH'], inherited['LD_LIBRARY_PATH'])
        self.assertEqual(env['LUA_CPATH'], str(build / '?.so') + ';;')


if __name__ == '__main__':
    unittest.main()
