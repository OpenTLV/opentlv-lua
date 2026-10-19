# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Small shared helpers for Lua-owned verification, never Core verification."""

import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from release_support import require, core_pin

ROOT = Path(__file__).resolve().parents[1]


def run(*args, cwd=ROOT, capture=False, env=None):
    command = list(map(str, args))
    print('+ ' + ' '.join(command), flush=True)
    result = subprocess.run(command, cwd=cwd, check=True, text=True, encoding='utf-8',
                            stdout=subprocess.PIPE if capture else None, env=env)
    return result.stdout.strip() if capture else None


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def revision(value):
    require(isinstance(value, str) and re.fullmatch('[0-9a-f]{40}', value), 'Expected full SHA')
    return value


def identity():
    return {'repository': os.environ['GITHUB_REPOSITORY'], 'commit': revision(os.environ['GITHUB_SHA']),
            'run_id': os.environ['GITHUB_RUN_ID'], 'run_attempt': os.environ['GITHUB_RUN_ATTEMPT']}


def same_identity(record):
    require(all(record.get(k) == v for k, v in identity().items()), 'Wrong repository/commit/run/attempt')


def pin(name):
    return core_pin((ROOT / 'cmake' / (name + '.cmake')).read_text(encoding='utf-8'))


def cache(build):
    return {m[1]: m[2] for line in (build / 'CMakeCache.txt').read_text(encoding='utf-8').splitlines()
            if (m := re.match(r'([^/#][^:]*):[^=]+=(.*)', line))}


def compiler(build):
    files = list((build / 'CMakeFiles').glob('*/CMakeCCompiler.cmake'))
    require(len(files) == 1, 'Expected one C compiler configuration')
    data = files[0].read_text(encoding='utf-8')
    result = {}
    for key in ('COMPILER_ID', 'COMPILER_VERSION', 'SIZEOF_DATA_PTR'):
        match = re.search(r'set\(CMAKE_C_' + key + r' "([^"]+)"\)', data)
        require(match is not None, 'Missing compiler metadata: ' + key)
        result[key] = match[1]
    return result


def api(path):
    return json.loads(run('gh', 'api', path, capture=True))
