#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Build and run the existing Lua suites against the same Core as the corpus."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def run(command, **kwargs):
    command = [str(part) for part in command]
    print("+ " + subprocess.list2cmdline(command), flush=True)
    return subprocess.run(command, cwd=ROOT, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lua-prefix", type=Path, required=True)
    parser.add_argument("--lua-include", type=Path)
    parser.add_argument("--lua-executable", type=Path)
    parser.add_argument("--busted", type=Path)
    parser.add_argument("--build-dir", type=Path, default=Path("build/ci"))
    parser.add_argument("--core-source", type=Path,
                        help="Explicit local Core for baseline comparisons; CI uses the pinned fetch")
    args = parser.parse_args()
    prefix = args.lua_prefix.resolve()
    build = args.build_dir.resolve()
    lua = (args.lua_executable or prefix / "bin" / ("lua.exe" if os.name == "nt" else "lua")).resolve()
    include = (args.lua_include or prefix / "include").resolve()
    busted = args.busted or shutil.which("busted")
    if not lua.is_file() or not (include / "lua.h").is_file():
        parser.error("Select a Lua interpreter and matching development headers")
    if not busted or not Path(busted).is_file():
        parser.error("Busted is required: install it with LuaRocks for the selected Lua runtime")
    busted = Path(busted).resolve()
    core = args.core_source.resolve() if args.core_source else build / "_deps/opentlv_core-src"

    # Lua 5.1 prints its version on stderr even on success.
    run([lua, "-v"], stderr=subprocess.STDOUT)
    run(["cmake", "-S", ROOT, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
         "-DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER",
         "-DFETCHCONTENT_FULLY_DISCONNECTED=OFF",
         "-DFETCHCONTENT_BASE_DIR=" + str(build / "_deps"),
         "-DFETCHCONTENT_SOURCE_DIR_OPENTLV_CORE=" + (str(core) if args.core_source else ""),
         "-DOPENTLV_BUILD_LUA_TESTS=ON", "-DLUA_INCLUDE_DIR=" + str(include),
         "-DOPENTLV_LUA_EXECUTABLE=" + str(lua),
         "-DOPENTLV_BUSTED_EXECUTABLE=" + str(busted)])
    run(["git", "-C", core, "rev-parse", "HEAD"])
    # A successful CTest run must include Busted, not silently skip the full suite.
    listing = run(["ctest", "--test-dir", build, "-C", "Release", "--show-only=json-v1"],
                  stdout=subprocess.PIPE, text=True)
    names = {test["name"] for test in json.loads(listing.stdout)["tests"]}
    if "Integration_lua" not in names:
        parser.error("The full Busted suite was not registered; CI requires full Core presets")

    run(["cmake", "--build", build, "--target", "opentlv_lua", "--config", "Release"])
    run(["ctest", "--test-dir", build, "-C", "Release", "--no-tests=error",
         "--output-on-failure", "--verbose"])
    output = build / "lua-query-conformance.lua"
    run([sys.executable, "-B", ROOT / "tests/query/lua_facade.py",
         "--core-source", core, "--output", output])
    env = os.environ.copy()
    env["LUA_PATH"] = ";".join((str(ROOT / "lua/?.lua"), str(ROOT / "lua/?/init.lua"),
                                env.get("LUA_PATH", ";")))
    suffix = ".dll" if os.name == "nt" else ".so"
    env["LUA_CPATH"] = str(build / ("?" + suffix)) + ";" + env.get("LUA_CPATH", ";")
    run([lua, output], env=env)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode)
