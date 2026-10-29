#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Generate a versioned LuaRocks rockspec from the development rockspec."""

import argparse
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
DEVELOPMENT = 'opentlv-scm-1.rockspec'
URL = 'git+https://github.com/OpenTLV/opentlv-lua.git'
TAG = re.compile(r'v((?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*))')


def replace_once(text, old, new, problem):
    if text.count(old) != 1:
        raise ValueError(problem)
    return text.replace(old, new)


def versioned(text, tag, revision=1):
    """Return the file name and content of the release rockspec for tag."""
    match = TAG.fullmatch(tag)
    if not match:
        raise ValueError('Lua release tags are vMAJOR.MINOR.PATCH, for example v0.3.0')
    if revision < 1:
        raise ValueError('The LuaRocks packaging revision starts at 1')
    version = f'{match[1]}-{revision}'
    text = replace_once(text, '\nversion = "scm-1"\n', f'\nversion = "{version}"\n',
                        'The development rockspec must declare version "scm-1" once')
    text = replace_once(text, f'   url = "{URL}"\n', f'   url = "{URL}",\n   tag = "{tag}"\n',
                        'The development rockspec must name only the standalone Git URL as its source')
    # Without this definition a build from LuaRocks' Git-less clone reports scm+unknown.
    text = replace_once(text, '"-DOPENTLV_LUA_RELEASE_TAG="', f'"-DOPENTLV_LUA_RELEASE_TAG={tag}"',
                        'The development rockspec must pass an empty OPENTLV_LUA_RELEASE_TAG once; '
                        'sources without it cannot report their release version from LuaRocks')
    return f'opentlv-{version}.rockspec', text


def development(tag=None):
    """Read the development rockspec of a release tag, or of the worktree."""
    if tag is None:
        return (ROOT / DEVELOPMENT).read_text(encoding='utf-8')
    return subprocess.run(['git', '-C', str(ROOT), 'show', f'refs/tags/{tag}:{DEVELOPMENT}'],
                          check=True, stdout=subprocess.PIPE, text=True, encoding='utf-8').stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag', help='Lua release tag, for example v0.3.0')
    parser.add_argument('--revision', type=int, default=1, help='LuaRocks packaging revision')
    parser.add_argument('--worktree', action='store_true',
                        help='Use the worktree rockspec instead of the one at the existing tag')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/rockspecs')
    args = parser.parse_args()
    try:
        name, text = versioned(development(None if args.worktree else args.tag), args.tag, args.revision)
    except ValueError as error:
        parser.error(f'{args.tag}: {error}')
    args.output.mkdir(parents=True, exist_ok=True)
    path = args.output / name
    with path.open('w', encoding='utf-8', newline='\n') as handle:
        handle.write(text)
    print(path)


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode)
