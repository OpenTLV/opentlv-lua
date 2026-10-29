# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Build and verify the Lua boundary, with explicit dependency and runtime evidence."""

import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

from lua_suite import EXPECTED_TESTS, checkout_sha, ctest_evidence, run, stream, summary
from ci_support import ROOT, compiler, pin, require, write
from verified_core import resolve


def runtime(lua, expected):
    result = run([lua, '-e', 'io.write(_VERSION); if jit then io.write(" ", jit.version) end'],
                 stdout=subprocess.PIPE, text=True).stdout
    if expected:
        require(('LuaJIT' in result) if expected == 'luajit' else
                (result == 'Lua ' + expected), 'Interpreter does not match the requested Lua runtime')
    return result


def lua_environment(build):
    env = {k: v for k, v in os.environ.items() if not k.startswith(('LUA_PATH', 'LUA_CPATH', 'LUA_INIT'))}
    env['LUA_PATH'] = str(ROOT / 'lua/?.lua') + ';' + str(ROOT / 'lua/?/init.lua') + ';;'
    env['LUA_CPATH'] = str(build / ('?.dll' if os.name == 'nt' else '?.so')) + ';;'
    if os.name == 'nt':
        # Lua's Windows loader searches PATH for dependencies of _core.dll.
        # CMake stages the selected shared Core DLL in this module directory.
        env['PATH'] = str(build / 'opentlv') + os.pathsep + env.get('PATH', '')
    return env


def execute(args, evidence):
    build = args.build_dir.resolve()
    prefix = args.lua_prefix.resolve()
    lua = (args.lua_executable or prefix / 'bin' / ('lua.exe' if os.name == 'nt' else 'lua')).resolve()
    include = (args.lua_include or prefix / 'include').resolve()
    if not args.lua_include and not (include / 'lua.h').is_file():
        candidates = list(include.glob('luajit-*/lua.h'))
        require(len(candidates) == 1, 'Expected one LuaJIT header directory')
        include = candidates[0].parent
    require(lua.is_file() and (include / 'lua.h').is_file(), 'Lua interpreter and matching headers required')
    evidence.update(lua_sha=checkout_sha(ROOT, os.environ.get('GITHUB_SHA') if args.release else None),
                    system=platform.system(), architecture=platform.machine(),
                    runtime=runtime(lua, args.runtime), requested_runtime=args.runtime,
                    shared=args.shared, sanitizer=args.sanitizer, configuration=args.configuration,
                    stage='dependency')
    require(not (args.release and args.core_source), 'Release forbids local Core overrides')
    if args.core_source:
        core = args.core_source.resolve()
        options = ['-DFETCHCONTENT_SOURCE_DIR_OPENTLV_CORE=' + str(core)]
        dependency = {'mode': 'local-source', 'verified': False, 'commit': checkout_sha(core)}
    else:
        options, dependency = resolve(build / 'verified-core', args.shared, args.configuration,
                                      required=args.release, source_only=args.sanitizer)
        core = Path(dependency.get('source_path', build / '_deps/opentlv_core-src'))
    evidence.update(core=dependency, core_sha=dependency['commit'], core_source=str(core))
    busted = args.busted or shutil.which('busted')
    require(args.smoke or (busted and Path(busted).is_file()), 'Busted is required for the complete suite')
    command = ['cmake', '-S', ROOT, '-B', build, '-DCMAKE_BUILD_TYPE=' + args.configuration,
               '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER',
               '-DFETCHCONTENT_SOURCE_DIR_OPENTLV_CORE=', '-DOPENTLV_LUA_CORE_PACKAGE=',
               '-DFETCHCONTENT_BASE_DIR=' + str(build / '_deps'),
               '-DOPENTLV_BUILD_SHARED_LIBS=' + args.shared,
               '-DOPENTLV_BUILD_LUA_TESTS=' + ('OFF' if args.smoke else 'ON'),
               '-DLUA_INCLUDE_DIR=' + str(include), '-DOPENTLV_LUA_EXECUTABLE=' + str(lua)]
    if busted:
        command.append('-DOPENTLV_BUSTED_EXECUTABLE=' + str(Path(busted).resolve()))
    if args.sanitizer:
        require(sys.platform == 'linux', 'Sanitizer runner is Linux only')
        flags = '-fsanitize=address,undefined -fno-omit-frame-pointer'
        command += ['-DCMAKE_C_FLAGS=' + flags, '-DCMAKE_SHARED_LINKER_FLAGS=' + flags,
                    '-DCMAKE_MODULE_LINKER_FLAGS=' + flags]
    run(command + options)
    if not dependency['verified']:
        actual = checkout_sha(core, pin('Core') if not args.core_source else None)
        require(actual == dependency['commit'], 'Core selection differs from recorded dependency')
    evidence.update(compiler=compiler(build), stage='build')
    run(['cmake', '--build', build, '--target', 'opentlv_lua', '--config', args.configuration, '--parallel', '2'])
    env = lua_environment(build)
    if args.sanitizer:
        # An instrumented module is dlopen'ed by an uninstrumented interpreter.
        # Preload the same compiler's runtime before executing any Lua process.
        cc = os.environ.get('CC', 'cc')
        library = 'libclang_rt.asan-x86_64.so' if 'clang' in cc else 'libasan.so'
        preload = run([cc, '-print-file-name=' + library], stdout=subprocess.PIPE, text=True).stdout.strip()
        require(Path(preload).is_file(), 'Cannot locate the sanitizer runtime')
        env.update(LD_PRELOAD=preload, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    evidence['stage'] = 'Lua tests'
    if args.smoke:
        run([lua, ROOT / 'tests/smoke.lua'], env=env)
        run([lua, ROOT / 'tests/version.lua', build / 'lua-version.json'], env=env)
        evidence['smoke'] = True
    else:
        listing = run(['ctest', '--test-dir', build, '-C', args.configuration, '--show-only=json-v1'],
                      stdout=subprocess.PIPE, text=True)
        tests = json.loads(listing.stdout)['tests']
        require(len(tests) == len(EXPECTED_TESTS) and {t['name'] for t in tests} == EXPECTED_TESTS,
                'Incomplete or unexpected Lua suite (Core suites must remain disabled)')
        result = stream(['ctest', '--test-dir', build, '-C', args.configuration, '--no-tests=error',
                         '--output-on-failure', '--verbose'], env=env)
        plain = ctest_evidence(result.stdout, evidence)
        result.check_returncode()
        require(evidence.get('ctest_passed') == evidence.get('ctest_total') == len(EXPECTED_TESTS) and
                evidence.get('busted_passed', 0) > 0 and
                all(evidence.get(k) == 0 for k in ('busted_failed', 'busted_errors', 'busted_pending')) and
                not re.search(r'\*\*\*\s*(?:Not Run|Skipped|Disabled)', plain), 'Incomplete Lua test evidence')
        output = build / 'lua-query-conformance.lua'
        run([sys.executable, '-B', ROOT / 'tests/query/lua_facade.py', '--core-source', core, '--output', output])
        result = stream([lua, output], env=env)
        result.check_returncode()
        counts = re.findall(r'^Lua public Query facade: (\d+) fixtures, (\d+) backend/window checks passed\s*$',
                            result.stdout, re.MULTILINE)
        require(len(counts) == 1 and min(map(int, counts[0])) > 0, 'Missing Query facade evidence')
        evidence.update(zip(('query_fixtures', 'query_checks'), map(int, counts[0])))
        evidence['tests'] = sorted(EXPECTED_TESTS)
    if args.rocks:
        evidence['stage'] = 'LuaRocks'
        # Always build the rock with static Core, from the same verified source.
        # Shared-library distribution is covered by the standalone build matrix.
        run([sys.executable, '-B', ROOT / 'scripts/check_luarocks.py', '--lua', lua, '--core-source', core])
        evidence['luarocks'] = True
        if args.shared == 'OFF':
            # The release rock always builds static Core, fetched as a consumer would.
            evidence['stage'] = 'Release rock'
            from check_release_rock import FIXTURE_TAG, check
            tag = os.environ['RELEASE_TAG'] if args.release else FIXTURE_TAG
            if args.release:
                tagged = run(['git', '-C', ROOT, 'rev-parse', f'refs/tags/{tag}^{{commit}}'],
                             stdout=subprocess.PIPE, text=True).stdout.strip()
                require(tagged == evidence['lua_sha'], 'The candidate tag must select the tested commit')
            evidence['release_rock'] = check(lua, core, evidence['lua_sha'], tag, build / 'lua-query-conformance.lua')
    if args.package:
        require(args.shared == 'OFF' and not args.sanitizer, 'Official module packages use static Core')
        from check_dependency import check
        evidence['dependency_checks'] = check(build, lua, include, core)
        from lua_package import package
        evidence['package'] = package(build, lua, core, args.package, evidence, args.release)
    if args.release:
        checkout_sha(ROOT, os.environ['GITHUB_SHA'])
    evidence.update(status='passed', stage='complete')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lua-prefix', type=Path, required=True)
    parser.add_argument('--lua-executable', type=Path)
    parser.add_argument('--lua-include', type=Path)
    parser.add_argument('--busted', type=Path)
    parser.add_argument('--build-dir', type=Path, default=Path('build/ci'))
    parser.add_argument('--core-source', type=Path)
    parser.add_argument('--runtime', default='')
    parser.add_argument('--shared', choices=('ON', 'OFF'), default='OFF')
    parser.add_argument('--configuration', choices=('Debug', 'Release'), default='Release')
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--sanitizer', action='store_true')
    parser.add_argument('--rocks', action='store_true')
    parser.add_argument('--release', action='store_true')
    parser.add_argument('--package', choices=('linux-x64-lua54', 'windows-x64-lua54'))
    parser.add_argument('--output', type=Path, default=Path('build/test-evidence.json'))
    args = parser.parse_args()
    evidence = {'schema': 1, 'status': 'failed', 'repository': os.environ.get('GITHUB_REPOSITORY', 'local'),
                'commit': os.environ.get('GITHUB_SHA', ''), 'run_id': os.environ.get('GITHUB_RUN_ID', ''),
                'run_attempt': os.environ.get('GITHUB_RUN_ATTEMPT', ''), 'id': os.environ.get('LUA_JOB_ID', 'local')}
    try:
        execute(args, evidence)
    finally:
        write(args.output, evidence)
        summary(evidence)


if __name__ == '__main__':
    main()
