#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Build the release source rock of one commit and install it into a fresh tree."""

import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

from ci_support import ROOT, digest, pin, require
from lua_package import PACKAGE_TESTS
from rockspec import DEVELOPMENT, TAG, versioned

# Full runs without a candidate tag declare this fixture release.
FIXTURE_TAG = 'v0.0.0'
# LuaRocks names the source directory of a Git URL after the repository.
SOURCE_DIR = 'opentlv-lua'
TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def git(*args, cwd=ROOT):
    # Never apply the runner's autocrlf: the rock must hold the committed bytes.
    return subprocess.run(['git', '-c', 'core.autocrlf=false', '-C', str(cwd), *args],
                          check=True, stdout=subprocess.PIPE).stdout


def run(command, **kwargs):
    command = [str(part) for part in command]
    print('+ ' + subprocess.list2cmdline(command), flush=True)
    return subprocess.run(command, check=True, **kwargs)


def source_rock(directory, commit, tag, root=ROOT):
    """Write the rockspec and .src.rock that `luarocks pack` makes from the tag.

    `luarocks pack` zips the rockspec with the shallow tag clone, from which it
    deletes .git and .gitignore. Without .gitattributes, `git archive` of the
    same commit holds the same files; stored entries in Git order with a fixed
    timestamp make the rock byte-identical on every platform.
    """
    require(TAG.fullmatch(tag), 'Release rocks require a vMAJOR.MINOR.PATCH tag')
    entries = [line.split(b'\t', 1)
               for line in git('ls-tree', '-r', '-z', '--full-tree', commit, cwd=root).split(b'\0') if line]
    paths = [path.decode('utf-8') for meta, path in entries]
    require(all(meta.split()[0] in (b'100644', b'100755') for meta, _ in entries),
            'Release rock sources must be regular files: no links or submodules')
    require('.gitattributes' not in paths, 'Export attributes would change the rock sources')
    name, text = versioned(git('show', f'{commit}:{DEVELOPMENT}', cwd=root).decode('utf-8'), tag)
    rockspec = directory / name
    rockspec.write_bytes(text.encode('utf-8'))
    rock = directory / name.replace('.rockspec', '.src.rock')
    archived = []
    with tarfile.open(fileobj=io.BytesIO(git('archive', '--format=tar', commit, cwd=root))) as tar, \
            zipfile.ZipFile(rock, 'w', zipfile.ZIP_STORED) as out:
        out.writestr(zip_entry(name, 0o644), text.encode('utf-8'))
        for member in tar.getmembers():
            if not member.isfile() or member.name == '.gitignore':
                continue
            archived.append(member.name)
            out.writestr(zip_entry(f'{SOURCE_DIR}/{member.name}', member.mode),
                         tar.extractfile(member).read())
    require(archived == [path for path in paths if path != '.gitignore'], 'Archive differs from the commit tree')
    return rockspec, rock


def zip_entry(name, mode):
    entry = zipfile.ZipInfo(name, TIMESTAMP)
    entry.external_attr = (0o100000 | (0o755 if mode & 0o111 else 0o644)) << 16
    entry.create_system = 3
    return entry


def lua_env(tree):
    env = {k: v for k, v in os.environ.items() if not k.startswith(('LUA_PATH', 'LUA_CPATH', 'LUA_INIT'))}
    pure = list(tree.glob('share/lua/*/opentlv/init.lua'))
    suffix = '.dll' if os.name == 'nt' else '.so'
    native = list(tree.glob('lib/lua/*/opentlv/_core' + suffix))
    require(len(pure) == 1 and len(native) == 1, 'The rock must install init.lua and the native module once')
    env['LUA_PATH'] = str(pure[0].parents[1] / '?/init.lua')
    env['LUA_CPATH'] = str(native[0].parents[1] / ('?' + suffix))
    return env, pure[0], native[0]


def check(lua, core, commit, tag, conformance, rocks='luarocks'):
    """Return evidence for the release rock of commit, installed from its .src.rock."""
    rocks = shutil.which(rocks)
    require(rocks, 'LuaRocks is required for the release rock')
    core_pin = pin('Core')
    # Short, as Windows builds Core below it; the space still tests command quoting.
    with tempfile.TemporaryDirectory(prefix='rock ') as temp:
        temp = Path(temp)
        rockspec, rock = source_rock(temp, commit, tag)
        run([rocks, 'lint', rockspec], cwd=temp)
        # `luarocks install` performs these two steps in a temporary directory;
        # running them here keeps the build for the Core pin check below.
        run([rocks, 'unpack', rock], cwd=temp)
        source = temp / rockspec.stem / SOURCE_DIR
        require(not (source / '.git').exists(), 'LuaRocks sources must not carry Git metadata')
        tree = temp / 'tree with space'
        run([rocks, '--tree=' + str(tree), 'make', rockspec.name], cwd=source)

        # The default Core path of a consumer: the pinned source, not a package.
        fetched = source / 'build/luarocks/_deps/opentlv_core-src'
        require(git('rev-parse', 'HEAD', cwd=fetched).decode().strip() == core_pin,
                'The release rock built a Core other than the pin')
        installed = list(tree.glob(f'lib/luarocks/rocks-*/opentlv/{rockspec.stem[len("opentlv-"):]}'))
        require(len(installed) == 1, 'Expected one installed release rock')
        installed = installed[0]
        require((installed / rockspec.name).read_bytes() == rockspec.read_bytes(), 'Installed rockspec differs')
        metadata = json.loads((installed / 'lua-version.json').read_text(encoding='utf-8'))
        require(metadata == {'binding_version': tag[1:], 'binding_revision': '', 'release_tag': tag,
                             'dirty': False}, 'Wrong release rock version metadata')
        require((installed / 'doc/LICENSE').read_bytes() == git('show', f'{commit}:LICENSE'),
                'The rock must install the Lua LICENSE')
        # The verified Core source and the fetched clone may differ only in line endings.
        license = (installed / 'doc/LICENSE-Core').read_bytes()
        require(license == (fetched / 'LICENSE').read_bytes() and license.replace(b'\r\n', b'\n') ==
                (core / 'LICENSE').read_bytes().replace(b'\r\n', b'\n'), 'The rock must install the pinned Core LICENSE')

        env, pure, native = lua_env(tree)
        require(pure.read_bytes() == git('show', f'{commit}:lua/opentlv/init.lua'), 'Installed facade differs')
        # Lua 5.1 has no package.searchpath: resolve the first matching cpath entry.
        run([lua, '-e', 'local m = require("opentlv"); assert(m == require("opentlv._core")); '
             'local found; for t in package.cpath:gmatch("[^;]+") do '
             'local p = t:gsub("%?", "opentlv" .. package.config:sub(1, 1) .. "_core"); '
             'local f = io.open(p, "rb"); if f then f:close(); found = p; break end end; '
             'assert(found == os.getenv("OPENTLV_ROCK_MODULE"), found); '
             'local v = os.getenv("OPENTLV_ROCK_VERSION"); assert(m.version() == v and m._VERSION == v); '
             'print("Release rock native module: " .. found)'],
            cwd=temp, env={**env, 'OPENTLV_ROCK_MODULE': str(native), 'OPENTLV_ROCK_VERSION': tag[1:]})
        for test in PACKAGE_TESTS:
            command = [lua, ROOT / 'tests' / test]
            if test == 'version.lua':
                command.append(installed / 'lua-version.json')
            run(command, cwd=temp, env=env)
        run([lua, conformance], cwd=temp, env=env)
        run([lua, ROOT / 'examples/quick_start.lua'], cwd=temp, env=env)
        return {'tag': tag, 'commit': commit, 'rock': rock.name, 'sha256': digest(rock),
                'rockspec_sha256': digest(rockspec), 'core_commit': core_pin,
                'tests': list(PACKAGE_TESTS), 'query': True, 'metadata': metadata}
