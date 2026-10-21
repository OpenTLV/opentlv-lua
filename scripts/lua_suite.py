#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Lua suite inventory, process execution and test evidence parsing."""

import os
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CORE_REPOSITORY = "https://github.com/OpenTLV/opentlv.git"
EXPECTED_TESTS = {
    "Integration_lua",
    "Integration_lua_version",
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
