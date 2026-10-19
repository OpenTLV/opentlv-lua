# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Exercise Lua's installed Core consumer and reject ambiguous selections."""

from pathlib import Path
import subprocess

from ci_support import ROOT, cache, require, run

SCENARIOS = ['installed-package-smoke', 'missing-explicit-package', 'package-source-conflict']


def check(build, lua, include, core):
    from ci import lua_environment
    settings = cache(build)
    package = settings.get('OPENTLV_LUA_CORE_PACKAGE')
    if not package:
        prefix = build / 'installed-core'
        run('cmake', '--install', build / '_deps/opentlv_core-build', '--prefix', prefix, '--config', 'Release')
        configs = list(prefix.rglob('OpenTLVConfig.cmake'))
        require(len(configs) == 1, 'Expected one installed Core config')
        package = str(configs[0].parent)
    consumer = build / 'package-consumer'
    base = ['cmake', '-S', str(ROOT), '-DCMAKE_BUILD_TYPE=Release', '-DOPENTLV_BUILD_LUA_TESTS=OFF',
            '-DLUA_INCLUDE_DIR=' + str(include), '-DOPENTLV_LUA_EXECUTABLE=' + str(lua)]
    run(*base, '-B', consumer, '-DOPENTLV_LUA_CORE_PACKAGE=' + package)
    run('cmake', '--build', consumer, '--target', 'opentlv_lua', '--config', 'Release', '--parallel', '2')
    run(lua, ROOT / 'tests/smoke.lua', env=lua_environment(consumer))
    for name, args, message in (
        ('missing', ['-DOPENTLV_LUA_CORE_PACKAGE=' + str(build / 'does-not-exist')], 'Explicit Core package'),
        ('conflict', ['-DOPENTLV_LUA_CORE_PACKAGE=' + package, '-DFETCHCONTENT_SOURCE_DIR_OPENTLV_CORE=' + str(core)],
         'cannot be combined')):
        result = subprocess.run(base + ['-B', str(build / ('reject-' + name))] + args,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        require(result.returncode != 0 and message in result.stdout, 'Dependency rejection failed: ' + name)
    return SCENARIOS
