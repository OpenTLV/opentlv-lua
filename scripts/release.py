#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Validate a Lua release against this Actions attempt and create only its tag."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from urllib.error import HTTPError
from urllib.request import Request, urlopen


REPOSITORY = "OpenTLV/opentlv-lua"
CORE_REPOSITORY = "OpenTLV/opentlv"
VERSION = r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"
SHA = r"[0-9a-f]{40}"
SYSTEMS = ("ubuntu-24.04", "windows-latest", "macos-latest")
LUA_VERSIONS = ("5.1", "5.3", "5.4")
EVIDENCE = re.compile(
    rf"Gate evidence \| Lua=({SHA}) \| Core=({SHA}) \| "
    r"CTest=([0-9]+)/([0-9]+) \| Busted=([0-9]+) \| Query=([0-9]+)/([0-9]+)")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def git(*args, cwd=None):
    return subprocess.check_output(["git", *args], cwd=cwd, text=True, encoding="utf-8").strip()


def api(path, data=None, missing=None):
    token = os.environ.get("GH_TOKEN", "")
    require(bool(token), "A GitHub token is required")
    request = Request("https://api.github.com/repos/" + path,
                      data=json.dumps(data).encode() if data is not None else None,
                      headers={"Authorization": "Bearer " + token,
                               "Accept": "application/vnd.github+json",
                               "X-GitHub-Api-Version": "2022-11-28",
                               "Content-Type": "application/json"})
    try:
        with urlopen(request, timeout=60) as response:
            return json.load(response)
    except HTTPError as error:
        if error.code == 404 and missing is not None:
            return missing
        raise ValueError(f"GitHub API {request.get_method()} {path}: HTTP {error.code}") from error


def core_pin(text):
    # Allow the documented trailing comment, but no mutable refs/extra arguments.
    pins = re.findall(r"^[ \t]*GIT_TAG\b([^\r\n]*)", text, re.MULTILINE)
    repositories = re.findall(r"^[ \t]*GIT_REPOSITORY\b([^\r\n]*)", text, re.MULTILINE)
    require(len(pins) == 1 and len(repositories) == 1, "Core must have one repository and immutable pin")
    match = re.fullmatch(rf"[ \t]+({SHA})[ \t]*(?:#[^\r\n]*)?", pins[0])
    require(match is not None, "Core GIT_TAG must be a full lowercase commit SHA")
    require(re.fullmatch(r"[ \t]+https://github\.com/OpenTLV/opentlv\.git[ \t]*(?:#[^\r\n]*)?",
                         repositories[0]) is not None, "Core must come from OpenTLV/opentlv")
    return match.group(1)


def request_identity(version, sha):
    require(re.fullmatch(VERSION, version), "Version/tag must be MAJOR.MINOR.PATCH, without v or leading zeros")
    require(re.fullmatch(SHA, sha), "Lua commit must be a full lowercase 40-character SHA")
    require(os.environ.get("GITHUB_REPOSITORY") == REPOSITORY, "Unexpected release repository")
    require(os.environ.get("GITHUB_EVENT_NAME") == "workflow_dispatch", "Release requires manual dispatch")
    require(os.environ.get("GITHUB_REF") == "refs/heads/main", "Dispatch release.yml from main")
    require(os.environ.get("GITHUB_WORKFLOW_REF") == REPOSITORY + "/.github/workflows/release.yml@refs/heads/main",
            "Unexpected release workflow")
    workflow_sha = os.environ.get("GITHUB_SHA", "")
    require(re.fullmatch(SHA, workflow_sha), "Missing workflow commit identity")
    require(git("rev-parse", "HEAD") == workflow_sha, "Release orchestration must use its trusted workflow commit")
    require(git("remote", "get-url", "origin") in ("https://github.com/" + REPOSITORY,
                                                   "https://github.com/" + REPOSITORY + ".git"),
            "Unexpected Lua checkout origin")
    require(git("rev-parse", sha + "^{commit}") == sha, "Requested object is not the exact Lua commit")
    subprocess.run(["git", "merge-base", "--is-ancestor", sha, workflow_sha], check=True)
    identity = {"version": version, "tag": version, "lua_sha": sha, "workflow_sha": workflow_sha,
                "core_sha": core_pin(git("show", sha + ":cmake/Core.cmake")),
                "run_id": os.environ.get("GITHUB_RUN_ID", ""),
                "run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT", "")}
    for field in ("run_id", "run_attempt"):
        require(re.fullmatch(r"[1-9][0-9]*", identity[field]), "Missing " + field)
    return identity


def verify_remote(identity):
    main = api(REPOSITORY + "/git/ref/heads/main")["object"]["sha"]
    comparison = api(REPOSITORY + "/compare/" + identity["lua_sha"] + "..." + main)
    require(comparison.get("status") in ("ahead", "identical") and
            comparison.get("base_commit", {}).get("sha") == identity["lua_sha"],
            "Requested Lua commit is not on the current main branch")
    core = api(CORE_REPOSITORY + "/commits/" + identity["core_sha"])
    require(core.get("sha") == identity["core_sha"], "Pinned Core commit is not in the expected repository")
    require(not api(REPOSITORY + "/git/ref/tags/" + identity["tag"], missing={}), "Release tag already exists")
    # Listing refs includes annotated tags; peel them before comparing commits.
    refs = api(REPOSITORY + "/git/matching-refs/tags/", missing=[])
    for ref in refs:
        if not re.fullmatch("refs/tags/" + VERSION, ref["ref"]):
            continue
        obj = ref["object"]
        for _ in range(10):
            if obj["type"] != "tag":
                break
            obj = api(REPOSITORY + "/git/tags/" + obj["sha"])["object"]
        require(obj["type"] == "commit", "Cannot resolve a release tag to a commit")
        require(obj["sha"] != identity["lua_sha"], "Lua commit already has a release tag")


def verify_checkout(sha, expected_core, core_path=None):
    require(re.fullmatch(SHA, sha) and re.fullmatch(SHA, expected_core), "Invalid expected checkout identities")
    require(Path(git("rev-parse", "--show-toplevel")).resolve() == Path.cwd().resolve(),
            "Lua source must be the checkout root")
    require(git("rev-parse", "HEAD") == sha, "Lua checkout does not match the requested SHA")
    require(not git("status", "--porcelain", "--untracked-files=normal"), "Lua release checkout is dirty")
    require(git("remote", "get-url", "origin") in ("https://github.com/" + REPOSITORY,
                                                   "https://github.com/" + REPOSITORY + ".git"),
            "Unexpected Lua checkout origin")
    require(core_pin(Path("cmake/Core.cmake").read_text(encoding="utf-8")) == expected_core,
            "Selected Core pin differs from the release request")
    if core_path:
        require(git("rev-parse", "HEAD", cwd=core_path) == expected_core, "Tested Core checkout differs from its pin")
        require(git("remote", "get-url", "origin", cwd=core_path) == "https://github.com/" + CORE_REPOSITORY + ".git",
                "Tested Core checkout has an unexpected origin")
        require(not git("status", "--porcelain", "--untracked-files=normal", cwd=core_path),
                "Tested Core checkout is dirty")


def attempt_jobs(identity, require_active=True):
    run = api(REPOSITORY + "/actions/runs/" + identity["run_id"])
    require(str(run.get("id")) == identity["run_id"] and
            str(run.get("run_attempt")) == identity["run_attempt"] and
            run.get("head_sha") == identity["workflow_sha"] and run.get("event") == "workflow_dispatch" and
            run.get("head_branch") == "main" and run.get("repository", {}).get("full_name") == REPOSITORY and
            run.get("path") == ".github/workflows/release.yml",
            "Actions run does not match this release request")
    if require_active:
        require(run.get("status") == "in_progress" and run.get("conclusion") is None,
                "Release run is no longer active")
    jobs = []
    for page in range(1, 101):
        batch = api(REPOSITORY + "/actions/runs/" + identity["run_id"] + "/attempts/" +
                    identity["run_attempt"] + f"/jobs?per_page=100&page={page}")
        jobs.extend(batch["jobs"])
        if len(jobs) >= batch["total_count"]:
            require(len(jobs) == batch["total_count"], "Inconsistent Actions jobs inventory")
            return jobs
    raise ValueError("Actions jobs inventory exceeded pagination limit")


def check_matrix(identity, jobs, rows):
    errors = []
    for system in SYSTEMS:
        for lua in LUA_VERSIONS:
            name = f"Full Lua CI / build and test / {system} / lua {lua}"
            matches = [job for job in jobs if job["name"] == name]
            row = {"name": name, "result": "missing", "counts": None}
            rows.append(row)
            try:
                require(len(matches) == 1, "Missing or duplicate matrix job")
                job = matches[0]
                row.update(result=job.get("conclusion") or job.get("status"), url=job.get("html_url", ""))
                require(str(job.get("run_id")) == identity["run_id"] and
                        str(job.get("run_attempt")) == identity["run_attempt"] and
                        job.get("head_sha") == identity["workflow_sha"], "Matrix job belongs to a different run/attempt")
                require(job.get("status") == "completed" and job.get("conclusion") == "success", "Job did not succeed")
                steps = job.get("steps", [])
                required = ["Verify release checkout", "Build and run existing Lua tests and Query corpus",
                            "Check Lua version metadata", "Build and check the installed LuaRocks package" +
                            (" on Windows" if system == "windows-latest" else "")]
                for required_name in required:
                    found = [step for step in steps if step["name"] == required_name]
                    require(len(found) == 1 and found[0].get("conclusion") == "success",
                            "Required step did not succeed: " + required_name)
                require(all(step.get("conclusion") in ("success", "skipped") for step in steps),
                        "Matrix job has incomplete or failed steps")
                markers = [step for step in steps if step["name"].startswith("Gate evidence |")]
                require(len(markers) == 1 and markers[0].get("conclusion") == "success", "Missing successful gate evidence")
                evidence = EVIDENCE.fullmatch(markers[0]["name"])
                require(evidence is not None, "Malformed gate evidence")
                require(evidence[1] == identity["lua_sha"] and evidence[2] == identity["core_sha"],
                        "Evidence identifies a different Lua/Core revision")
                passed, total, busted, fixtures, checks = map(int, evidence.groups()[2:])
                require(passed == total == 15 and min(busted, fixtures, checks) > 0,
                        "Incomplete CTest/Busted/Query results")
                row["counts"] = f"CTest {passed}/{total}; Busted {busted}; Query {fixtures} fixtures / {checks} checks"
            except ValueError as error:
                row["error"] = str(error)
                errors.append(name + ": " + str(error))
    require(not errors, "; ".join(errors))


def summary(identity, rows, status, error=""):
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not path:
        return
    lines = ["## Lua release gate", "", f"Result: **{status}**", ""]
    for key in ("version", "tag", "lua_sha", "core_sha", "workflow_sha", "run_id", "run_attempt"):
        lines.append(f"- {key}: `{identity.get(key, 'unavailable')}`")
    lines += ["", "| Required job | Result | Test summary |", "| --- | --- | --- |"]
    for row in rows:
        label = row["name"].replace("|", "\\|")
        if row.get("url"):
            label = f"[{label}]({row['url']})"
        detail = row.get("error") or row.get("counts") or "unavailable"
        lines.append(f"| {label} | {row['result']} | {detail} |")
    if error:
        lines += ["", "Failure: " + error.replace("\n", " ")]
    lines += ["", "Per-job summaries contain the existing Lua test results. No distribution artifacts are uploaded.", ""]
    with open(path, "a", encoding="utf-8") as stream:
        stream.write("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "finish", "verify"))
    parser.add_argument("--version", default=os.environ.get("RELEASE_VERSION", ""))
    parser.add_argument("--sha", default=os.environ.get("RELEASE_LUA_SHA", ""))
    parser.add_argument("--core-sha", default=os.environ.get("RELEASE_CORE_SHA", ""))
    parser.add_argument("--core-path", type=Path)
    args = parser.parse_args()
    if args.command == "verify":
        verify_checkout(args.sha, args.core_sha, args.core_path)
        return
    identity = {"version": args.version, "tag": args.version, "lua_sha": args.sha}
    rows, status, failure = [], "failed; no tag created", ""
    try:
        identity = request_identity(args.version, args.sha)
        if args.command == "prepare":
            verify_remote(identity)
            with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
                for key in ("lua_sha", "core_sha", "version"):
                    stream.write(key + "=" + identity[key] + "\n")
            status = "request validated; no tag created"
        else:
            require(identity["core_sha"] == args.core_sha, "Core pin differs from the prepared request")
            jobs = attempt_jobs(identity, require_active=False)
            check_matrix(identity, jobs, rows)
            require(os.environ.get("PREPARE_RESULT") == "success" and os.environ.get("FULL_CI_RESULT") == "success",
                    "Required preparation or Full CI did not succeed")
            verify_remote(identity)
            # Recheck run state immediately before the only remote mutation.
            attempt_jobs(identity)
            status = "tag creation requested; outcome unconfirmed"
            created = api(REPOSITORY + "/git/refs", {"ref": "refs/tags/" + identity["tag"], "sha": identity["lua_sha"]})
            status = "tag created"
            require(created.get("ref") == "refs/tags/" + identity["tag"] and
                    created.get("object", {}).get("sha") == identity["lua_sha"], "Unexpected tag creation response")
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        failure = str(error)
        print("Release gate: " + failure, file=sys.stderr)
        raise SystemExit(1) from error
    finally:
        summary(identity, rows, status, failure)


if __name__ == "__main__":
    main()
