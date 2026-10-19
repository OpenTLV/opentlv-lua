#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Install the rock and load both modules using only a fresh LuaRocks tree."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def run(command, **kwargs):
    command = [str(part) for part in command]
    print("+ " + subprocess.list2cmdline(command), flush=True)
    return subprocess.run(command, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lua", type=Path, required=True)
    parser.add_argument("--luarocks", default="luarocks")
    parser.add_argument("--core-source", type=Path,
                        help="Preconfigure the rock build with this explicit Core checkout")
    args = parser.parse_args()
    lua = args.lua.resolve()
    rocks = shutil.which(args.luarocks)
    if not lua.is_file() or not rocks:
        parser.error("Lua and LuaRocks must be installed for the same runtime")
    # A local override reuses the exact Core already tested by CI. Without an
    # override this exercises the unmodified rockspec's default dependency path.
    if args.core_source:
        include = run([rocks, "config", "variables.LUA_INCDIR"],
                      stdout=subprocess.PIPE, text=True).stdout.strip()
        run(["cmake", "--fresh", "-S", ROOT, "-B", ROOT / "build/luarocks",
             "-DCMAKE_BUILD_TYPE=Release", "-DOPENTLV_BUILD_LUA_TESTS=OFF", "-DOPENTLV_BUILD_SHARED_LIBS=OFF",
             "-DLUA_INCLUDE_DIR=" + include,
             "-DFETCHCONTENT_SOURCE_DIR_OPENTLV_CORE=" + str(args.core_source.resolve())],
            cwd=ROOT)
    run([rocks, "lint", "opentlv-scm-1.rockspec"], cwd=ROOT)
    # Spaces in the destination exercise quoting in the rockspec commands.
    with tempfile.TemporaryDirectory(prefix="opentlv rocks ") as directory:
        tree = Path(directory) / "tree"
        run([rocks, "--tree=" + str(tree), "make", "opentlv-scm-1.rockspec"], cwd=ROOT)
        pure = list(tree.glob("share/lua/*/opentlv/init.lua"))
        suffix = ".dll" if os.name == "nt" else ".so"
        native = list(tree.glob("lib/lua/*/opentlv/_core" + suffix))
        metadata = list(tree.glob("lib/luarocks/rocks-*/opentlv/scm-1/lua-version.json"))
        if len(pure) != 1 or len(native) != 1 or len(metadata) != 1:
            raise RuntimeError("The rock must install init.lua, the native module, and version metadata")
        built = json.loads((ROOT / "build/luarocks/lua-version.json").read_text(encoding="utf-8"))
        installed = json.loads(metadata[0].read_text(encoding="utf-8"))
        if installed != built:
            raise RuntimeError("Installed Lua version metadata differs from the build")
        print("Installed Lua metadata: " + json.dumps(installed), flush=True)
        # Clear ABI-specific variables too: Lua gives them priority over LUA_PATH.
        env = {key: value for key, value in os.environ.items()
               if not key.startswith(("LUA_PATH", "LUA_CPATH", "LUA_INIT"))}
        env["LUA_PATH"] = str(pure[0].parents[1] / "?/init.lua")
        env["LUA_CPATH"] = str(native[0].parents[1] / ("?" + suffix))
        run([lua, "-e", 'local m = require("opentlv"); '
             'assert(m == require("opentlv._core")); '
             'assert(type(m.version()) == "string" and #m.version() > 0); '
             'assert(m.version() == m._VERSION); '
             'print("Installed module Core version: " .. m.version())'],
            cwd=directory, env=env)
        run([lua, ROOT / "examples/quick_start.lua"], cwd=directory, env=env)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode)
