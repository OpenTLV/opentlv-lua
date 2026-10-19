# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Analyze only the Lua binding's translation units using the actual build flags."""

import argparse
from pathlib import Path
import shutil

from ci_support import ROOT, read, require, run, write


def sources(build):
    items = [item for item in read(build / 'compile_commands.json')
             if Path(item['file']).resolve().is_relative_to(ROOT / 'src')]
    require({Path(item['file']).resolve() for item in items} == set((ROOT / 'src').rglob('*.c')),
            'Static analysis requires every Lua translation unit')
    return items


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--tool', choices=('cppcheck', 'clang-tidy'), required=True)
    args = parser.parse_args()
    items = sources(args.build)
    if args.tool == 'cppcheck':
        database = args.build / 'lua-compile-commands.json'
        write(database, items)
        run('cppcheck', '--project=' + str(database), '--enable=warning,performance,portability',
            '--error-exitcode=1', '--std=c99', '--inline-suppr', '--suppress=missingIncludeSystem',
            '--suppress=normalCheckLevelMaxBranches')
    else:
        executable = shutil.which('clang-tidy-18') or shutil.which('clang-tidy')
        require(executable, 'clang-tidy 18 is required')
        for item in items:
            run(executable, '-quiet', '-p', args.build, '--warnings-as-errors=*',
                '--header-filter=' + str(ROOT / 'src'), item['file'])


if __name__ == '__main__':
    main()
