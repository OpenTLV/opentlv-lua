#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Build and run the existing Lua suites against the same Core as the corpus."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
CORE_REPOSITORY = "https://github.com/OpenTLV/opentlv.git"
EXPECTED_TESTS = {
    "Integration_lua",
    "Integration_lua_query_document",
    "Integration_lua_query_document_allocator",
    "Integration_lua_codec",
    "Integration_lua_codec_allocator",
    "Integration_example_lua_codec",
    "Integration_lua_schema",
    "Integration_example_lua_schema",
    "Integration_lua_visitor",
    "Integration_lua_visitor_allocator",
    "Integration_lua_writer",
    "Integration_lua_nfc_type2",
    "Integration_lua_components",
    "Integration_example_lua_quick_start",
    "Integration_example_lua_parse",
}


def run(command, **kwargs):
    command = [str(part) for part in command]
    print("+ " + subprocess.list2cmdline(command), flush=True)
    return subprocess.run(command, cwd=ROOT, check=True, **kwargs)


def stream(command, **kwargs):
    """Keep the hosted log live while retaining the existing suite's evidence."""
    command = [str(part) for part in command]
    print("+ " + subprocess.list2cmdline(command), flush=True)
    lines = []
    with subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                          errors="replace", bufsize=1, **kwargs) as process:
        for line in process.stdout:
            print(line, end="", flush=True)
            lines.append(line)
        returncode = process.wait()
    return subprocess.CompletedProcess(command, returncode, "".join(lines))


def checkout_sha(source, expected=None, canonical_core=False):
    toplevel = run(["git", "-C", source, "rev-parse", "--show-toplevel"],
                   stdout=subprocess.PIPE, text=True).stdout.strip()
    if Path(toplevel).resolve() != source.resolve():
        raise RuntimeError(f"Expected a Git checkout at {source}; found {toplevel}")
    sha = run(["git", "-C", source, "rev-parse", "HEAD"],
              stdout=subprocess.PIPE, text=True).stdout.strip()
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise RuntimeError(f"Invalid checkout SHA for {source}: {sha}")
    if expected is not None:
        if sha != expected:
            raise RuntimeError(f"Checkout {source} is {sha}; expected {expected}")
        dirty = run(["git", "-C", source, "status", "--porcelain", "--untracked-files=all"],
                    stdout=subprocess.PIPE, text=True).stdout.strip()
        if dirty:
            raise RuntimeError(f"Release checkout is dirty: {source}")
    if canonical_core:
        origin = run(["git", "-C", source, "remote", "get-url", "origin"],
                     stdout=subprocess.PIPE, text=True).stdout.strip()
        if origin != CORE_REPOSITORY:
            raise RuntimeError(f"Release Core origin must be {CORE_REPOSITORY}; got {origin}")
    return sha


def ctest_evidence(output, evidence):
    # Busted may color its summary when the runner is configured for ANSI output.
    plain = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", output)
    summaries = re.findall(
        r"^(\d+)% tests passed(?:, (\d+) tests failed)? out of (\d+)\s*$",
        plain, re.MULTILINE)
    if len(summaries) == 1:
        percentage, failed, total = summaries[0]
        # Newer CTest omits the zero-failure clause for a fully passing suite.
        if percentage == "100" or (failed and int(failed) > 0):
            failed, total = int(failed or 0), int(total)
            evidence.update(ctest_passed=total - failed, ctest_total=total)
    busted = re.findall(
        r"^\s*(?:\d+:\s*)?(\d+) success(?:es)? / (\d+) failures? / (\d+) errors? / (\d+) pending\b",
        plain, re.MULTILINE)
    if len(busted) == 1:
        passed, failed, errors, pending = map(int, busted[0])
        evidence.update(busted_passed=passed, busted_failed=failed,
                        busted_errors=errors, busted_pending=pending)
    return plain


def summary(evidence):
    target = os.environ.get("GITHUB_STEP_SUMMARY")
    if not target:
        return

    def value(key):
        return str(evidence.get(key, "not available")).replace("|", "\\|").replace("\n", " ")

    lines = ["### Lua test evidence", "", f"Result: **{value('status')}**", "",
             "| Evidence | Result |", "| --- | --- |"]
    if os.environ.get("RELEASE_VERSION"):
        lines.append(f"| Release candidate | `{os.environ['RELEASE_VERSION']}` |")
    lines.extend([
        f"| Lua commit | `{value('lua_sha')}` |",
        f"| Core commit | `{value('core_sha')}` |",
        f"| Core source | `{value('core_source')}` |",
        f"| CTest | {value('ctest_passed')}/{value('ctest_total')} passed |",
        f"| Busted | {value('busted_passed')} passed, {value('busted_failed')} failures, "
        f"{value('busted_errors')} errors, {value('busted_pending')} pending |",
        f"| Query corpus | {value('query_fixtures')} fixtures, {value('query_checks')} checks passed |",
        "",
    ])
    if evidence.get("canonical_core"):
        lines.extend([f"Verified Core origin: {CORE_REPOSITORY}", ""])
    if "error" in evidence:
        lines.extend([f"Failed during **{value('stage')}**: {value('error')}", ""])
    with open(target, "a", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")


def execute(evidence):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lua-prefix", type=Path, required=True)
    parser.add_argument("--lua-include", type=Path)
    parser.add_argument("--lua-executable", type=Path)
    parser.add_argument("--busted", type=Path)
    parser.add_argument("--build-dir", type=Path, default=Path("build/ci"))
    parser.add_argument("--core-source", type=Path,
                        help="Explicit local Core for baseline comparisons; CI uses the pinned fetch")
    args = parser.parse_args()
    # Workflow expressions are empty on ordinary push/PR runs.
    expected_lua = os.environ.get("RELEASE_LUA_SHA") or None
    expected_core = os.environ.get("RELEASE_CORE_SHA") or None
    release = expected_lua is not None or expected_core is not None
    if release and not all(re.fullmatch(r"[0-9a-f]{40}", sha or "")
                           for sha in (expected_lua, expected_core)):
        raise RuntimeError("Release CI requires both RELEASE_LUA_SHA and RELEASE_CORE_SHA as full lowercase SHAs")
    evidence["stage"] = "Lua checkout verification"
    evidence["lua_sha"] = checkout_sha(ROOT, expected_lua)
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
    evidence["core_source"] = str(core)

    # Lua 5.1 prints its version on stderr even on success.
    evidence["stage"] = "configuration"
    run([lua, "-v"], stderr=subprocess.STDOUT)
    run(["cmake", "-S", ROOT, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
         "-DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER",
         "-DFETCHCONTENT_FULLY_DISCONNECTED=OFF",
         "-DFETCHCONTENT_BASE_DIR=" + str(build / "_deps"),
         "-DFETCHCONTENT_SOURCE_DIR_OPENTLV_CORE=" + (str(core) if args.core_source else ""),
         "-DOPENTLV_BUILD_LUA_TESTS=ON", "-DLUA_INCLUDE_DIR=" + str(include),
         "-DOPENTLV_LUA_EXECUTABLE=" + str(lua),
         "-DOPENTLV_BUSTED_EXECUTABLE=" + str(busted)])
    evidence["stage"] = "Core checkout verification"
    evidence["core_sha"] = checkout_sha(core, expected_core, canonical_core=release)
    evidence["canonical_core"] = release
    evidence["stage"] = "test registration"
    # Require the complete standalone suite, without enabling Core's own tests.
    listing = run(["ctest", "--test-dir", build, "-C", "Release", "--show-only=json-v1"],
                  stdout=subprocess.PIPE, text=True)
    tests = json.loads(listing.stdout)["tests"]
    names = {test["name"] for test in tests}
    if names != EXPECTED_TESTS or len(tests) != len(EXPECTED_TESTS):
        raise RuntimeError("The complete standalone CTest suite must be registered; "
                           f"missing: {sorted(EXPECTED_TESTS - names)}, "
                           f"unexpected: {sorted(names - EXPECTED_TESTS)}")

    evidence["stage"] = "build"
    run(["cmake", "--build", build, "--target", "opentlv_lua", "--config", "Release"])
    evidence["stage"] = "CTest and Busted"
    ctest = stream(["ctest", "--test-dir", build, "-C", "Release", "--no-tests=error",
                    "--output-on-failure", "--verbose"])
    plain = ctest_evidence(ctest.stdout, evidence)
    ctest.check_returncode()
    if (evidence.get("ctest_passed") != len(EXPECTED_TESTS)
            or evidence.get("ctest_total") != len(EXPECTED_TESTS)
            or not evidence.get("busted_passed", 0)
            or any(evidence.get(key) != 0 for key in ("busted_failed", "busted_errors", "busted_pending"))
            or re.search(r"\*\*\*\s*(?:Not Run|Skipped|Disabled)", plain)):
        raise RuntimeError("CTest and Busted must report the full suite passing without skipped tests")
    evidence["stage"] = "Query corpus generation"
    output = build / "lua-query-conformance.lua"
    run([sys.executable, "-B", ROOT / "tests/query/lua_facade.py",
         "--core-source", core, "--output", output])
    env = os.environ.copy()
    env["LUA_PATH"] = ";".join((str(ROOT / "lua/?.lua"), str(ROOT / "lua/?/init.lua"),
                                env.get("LUA_PATH", ";")))
    suffix = ".dll" if os.name == "nt" else ".so"
    env["LUA_CPATH"] = str(build / ("?" + suffix)) + ";" + env.get("LUA_CPATH", ";")
    evidence["stage"] = "Query corpus"
    query = stream([lua, output], env=env)
    counts = re.findall(r"^Lua public Query facade: (\d+) fixtures, (\d+) backend/window checks passed\s*$",
                        query.stdout, re.MULTILINE)
    if len(counts) == 1:
        evidence.update(zip(("query_fixtures", "query_checks"), map(int, counts[0])))
    query.check_returncode()
    if not evidence.get("query_fixtures", 0) or not evidence.get("query_checks", 0):
        raise RuntimeError("The Query corpus did not report positive fixture and check counts")
    if release:
        evidence["stage"] = "final checkout verification"
        checkout_sha(ROOT, expected_lua)
        checkout_sha(core, expected_core, canonical_core=True)
    # Downstream release steps can consume evidence only after every suite passed.
    evidence["stage"] = "publishing test evidence"
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as handle:
            for key in ("lua_sha", "core_sha", "ctest_passed", "ctest_total", "busted_passed",
                        "query_fixtures", "query_checks"):
                handle.write(f"{key}={evidence[key]}\n")


def main():
    evidence = {"status": "failed", "stage": "arguments"}
    try:
        execute(evidence)
        evidence["status"] = "passed"
    except BaseException as error:
        evidence["error"] = str(error)
        raise
    finally:
        summary(evidence)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode)
    except RuntimeError as error:
        raise SystemExit(str(error))
