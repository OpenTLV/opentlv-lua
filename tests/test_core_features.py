# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Exercise the Lua CMake boundary with controlled Core source/package fixtures."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FEATURES = json.loads((ROOT / 'cmake/core-features.json').read_text(encoding='utf-8'))


class CoreFeatures(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'consumer'
        self.source.mkdir()
        # Include the actual dependency boundary without needing Lua or a compiler.
        (self.source / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.24)\nproject(Consumer NONE)\n' +
            ''.join(f'set({feature} OFF)\n' for feature in FEATURES) +
            f'include("{(ROOT / "cmake/Core.cmake").as_posix()}")\n', encoding='utf-8')

    def configure(self, *options):
        return subprocess.run(
            ['cmake', '-S', str(self.source), '-B', str(self.root / 'build'), *options],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def package(self, definitions):
        package = self.root / 'package'
        header = package / 'include/tlv/config.h'
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text(definitions, encoding='utf-8')
        (package / 'OpenTLVConfig.cmake').write_text(
            'add_library(OpenTLV::tlv INTERFACE IMPORTED)\n'
            'set_target_properties(OpenTLV::tlv PROPERTIES INTERFACE_INCLUDE_DIRECTORIES '
            '"${CMAKE_CURRENT_LIST_DIR}/include")\n', encoding='utf-8')
        return '-DOPENTLV_LUA_CORE_PACKAGE=' + package.as_posix()

    def test_source_enables_features_despite_upstream_defaults_and_stale_cache(self):
        core = self.root / 'core'
        core.mkdir()
        (core / 'config.h.in').write_text(
            ''.join(f'#cmakedefine01 {feature}\n' for feature in FEATURES), encoding='utf-8')
        (core / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.24)\nproject(Core NONE)\n' +
            ''.join(f'option({feature} "Upstream fixture defaults to OFF" OFF)\n' for feature in FEATURES) +
            'configure_file(config.h.in include/tlv/config.h)\n'
            'add_library(tlv INTERFACE)\nadd_library(OpenTLV::tlv ALIAS tlv)\n'
            'target_include_directories(tlv INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>")\n',
            encoding='utf-8')
        options = ['-DFETCHCONTENT_SOURCE_DIR_OPENTLV_CORE=' + core.as_posix()]
        for _ in range(2):
            result = self.configure(*options, *(f'-D{feature}:BOOL=OFF' for feature in FEATURES))
            self.assertEqual(result.returncode, 0, result.stdout)
            cache = (self.root / 'build/CMakeCache.txt').read_text(encoding='utf-8')
            for feature in FEATURES:
                self.assertIn(f'{feature}:INTERNAL=ON', cache)

    def test_complete_package_ignores_consumer_feature_cache(self):
        option = self.package(''.join(f'#define {feature} 1\n' for feature in FEATURES))
        result = self.configure(option, *(f'-D{feature}=OFF' for feature in FEATURES))
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_incomplete_package_cannot_be_repaired_by_consumer_flags(self):
        for feature in FEATURES:
            for definition in ('disabled', 'missing', 'duplicate'):
                with self.subTest(feature=feature, definition=definition):
                    header = ''.join(f'#define {name} 1\n' for name in FEATURES if name != feature)
                    if definition == 'disabled':
                        header += f'#define {feature} 0\n'
                    elif definition == 'duplicate':
                        header += f'#define {feature} 1\n#define {feature} 0\n'
                    result = self.configure(self.package(header), f'-D{feature}=ON')
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertIn(feature, result.stdout)
                    self.assertIn('requires' if definition == 'disabled' else 'unique', result.stdout)


if __name__ == '__main__':
    unittest.main()
