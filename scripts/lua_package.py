# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Package and reload the static-Core module from an isolated extracted tree."""

import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import zipfile

from ci_support import ROOT, digest, read, require, write

PACKAGE_TESTS = ('smoke.lua', 'version.lua', 'codec_spec.lua', 'query_document_spec.lua',
                 'schema_spec.lua', 'writer_spec.lua', 'visitor_spec.lua', 'program_spec.lua')


def package(build, lua, core, identifier, evidence, release):
    require(evidence['requested_runtime'] == '5.4' and evidence['runtime'] == 'Lua 5.4' and
            evidence['compiler']['SIZEOF_DATA_PTR'] == '8' and
            platform.machine().lower() in ('amd64', 'x86_64') and platform.system() in ('Linux', 'Windows'),
            'Package requires Linux/Windows x64 Lua 5.4')
    require(identifier == ('windows-x64-lua54' if os.name == 'nt' else 'linux-x64-lua54'), 'Wrong package OS')
    metadata = read(build / 'lua-version.json')
    if release:
        require(metadata['binding_version'] == os.environ['RELEASE_TAG'][1:] and not metadata['dirty'] and
                metadata['binding_revision'] == evidence['commit'], 'Wrong release module metadata')
    version = os.environ.get('RELEASE_TAG', 'development').removeprefix('v')
    destination = ROOT / 'build/packages'
    destination.mkdir(parents=True, exist_ok=True)
    archive = destination / f'OpenTLV-Lua-{version}-{identifier}.zip'
    with tempfile.TemporaryDirectory(prefix='lua package ') as temp:
        tree = Path(temp) / 'stage'
        (tree / 'opentlv').mkdir(parents=True)
        suffix = '.dll' if os.name == 'nt' else '.so'
        shutil.copyfile(build / ('opentlv/_core' + suffix), tree / ('opentlv/_core' + suffix))
        shutil.copyfile(ROOT / 'lua/opentlv/init.lua', tree / 'opentlv/init.lua')
        shutil.copyfile(build / 'lua-version.json', tree / 'lua-version.json')
        shutil.copyfile(ROOT / 'LICENSE', tree / 'LICENSE-Lua')
        shutil.copyfile(core / 'LICENSE', tree / 'LICENSE-Core')
        write(tree / 'build-identity.json', {k: evidence[k] for k in
              ('commit', 'core', 'runtime', 'compiler', 'configuration', 'shared', 'system', 'architecture')})
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as out:
            for path in sorted(tree.rglob('*')):
                if path.is_file():
                    out.write(path, path.relative_to(tree).as_posix())
        extracted = Path(temp) / 'extracted'
        with zipfile.ZipFile(archive) as source:
            source.extractall(extracted)  # Only our freshly created, enumerated archive.
        env = {k: v for k, v in os.environ.items() if not k.startswith(('LUA_PATH', 'LUA_CPATH', 'LUA_INIT'))}
        env['LUA_PATH'] = str(extracted / '?/init.lua')
        env['LUA_CPATH'] = str(extracted / ('?' + suffix))
        for test in PACKAGE_TESTS:
            command = [str(lua), str(ROOT / 'tests' / test)]
            if test == 'version.lua':
                command.append(str(extracted / 'lua-version.json'))
            subprocess.run(command, cwd=extracted, env=env, check=True)
        subprocess.run([str(lua), str(build / 'lua-query-conformance.lua')], cwd=extracted, env=env, check=True)
    return {'id': identifier, 'filename': archive.name, 'sha256': digest(archive),
            'tests': list(PACKAGE_TESTS), 'query': True, 'metadata': metadata}
