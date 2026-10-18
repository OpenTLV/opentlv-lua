#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Exercise release entry points with mocked Git and GitHub; never create tags."""

from contextlib import redirect_stderr
from copy import deepcopy
from io import StringIO
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import release


LUA_SHA = "a" * 40
CORE_SHA = "b" * 40
WORKFLOW_SHA = "c" * 40
OTHER_SHA = "d" * 40
REPOSITORY = "OpenTLV/opentlv-lua"


def successful_jobs():
    jobs = []
    for system in ("ubuntu-24.04", "windows-latest", "macos-latest"):
        for lua in ("5.1", "5.3", "5.4"):
            names = ["Verify release checkout", "Build and run existing Lua tests and Query corpus",
                     "Check Lua version metadata", "Build and check the installed LuaRocks package" +
                     (" on Windows" if system == "windows-latest" else ""),
                     f"Gate evidence | Lua={LUA_SHA} | Core={CORE_SHA} | CTest=15/15 | Busted=17 | Query=23/41"]
            jobs.append({"name": f"Full Lua CI / build and test / {system} / lua {lua}",
                         "run_id": 123, "run_attempt": 1, "head_sha": WORKFLOW_SHA,
                         "status": "completed", "conclusion": "success",
                         "steps": [{"name": name, "conclusion": "success"} for name in names]})
    return jobs


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="lua release ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.environment = {
            "GITHUB_REPOSITORY": REPOSITORY, "GITHUB_EVENT_NAME": "workflow_dispatch",
            "GITHUB_REF": "refs/heads/main", "GITHUB_SHA": WORKFLOW_SHA,
            "GITHUB_WORKFLOW_REF": REPOSITORY + "/.github/workflows/release.yml@refs/heads/main",
            "GITHUB_RUN_ID": "123", "GITHUB_RUN_ATTEMPT": "1",
            "GITHUB_OUTPUT": str(self.root / "output"),
            "GITHUB_STEP_SUMMARY": str(self.root / "summary"),
            "PREPARE_RESULT": "success", "FULL_CI_RESULT": "success"}
        self.jobs = successful_jobs()
        self.run = {"id": 123, "run_attempt": 1, "head_sha": WORKFLOW_SHA,
                    "event": "workflow_dispatch", "head_branch": "main",
                    "repository": {"full_name": REPOSITORY}, "path": ".github/workflows/release.yml",
                    "status": "in_progress", "conclusion": None}
        self.final_run = None
        self.existing_tag = False
        self.refs = []
        self.annotated_tags = {}
        self.git_head = WORKFLOW_SHA
        self.git_candidate = LUA_SHA
        self.ancestor = True
        self.comparison = {"status": "ahead", "base_commit": {"sha": LUA_SHA}}
        self.core_pin = CORE_SHA

    def git(self, *args, **kwargs):
        self.assertFalse(kwargs)
        if args == ("rev-parse", "HEAD"):
            return self.git_head
        if args == ("rev-parse", LUA_SHA + "^{commit}"):
            return self.git_candidate
        if args == ("remote", "get-url", "origin"):
            return "https://github.com/" + REPOSITORY + ".git"
        if args == ("show", LUA_SHA + ":cmake/Core.cmake"):
            return ("GIT_REPOSITORY https://github.com/OpenTLV/opentlv.git\n"
                    "GIT_TAG " + self.core_pin + " # immutable Core baseline\n")
        self.fail(f"Unexpected Git call: {args}")

    def api(self, path, data=None, missing=None):
        self.requests.append((path, data))
        if data is not None:
            self.posts.append((path, data))
            self.assertEqual(path, REPOSITORY + "/git/refs")
            return {"ref": data["ref"], "object": {"sha": data["sha"]}}
        if path == REPOSITORY + "/git/ref/heads/main":
            return {"object": {"sha": WORKFLOW_SHA}}
        if path == REPOSITORY + "/compare/" + LUA_SHA + "..." + WORKFLOW_SHA:
            return self.comparison
        if path == "OpenTLV/opentlv/commits/" + CORE_SHA:
            return {"sha": CORE_SHA}
        if path.startswith(REPOSITORY + "/git/ref/tags/"):
            return {"ref": path} if self.existing_tag else missing
        if path == REPOSITORY + "/git/matching-refs/tags/":
            return self.refs
        if path.startswith(REPOSITORY + "/git/tags/"):
            return {"object": self.annotated_tags[path.rsplit("/", 1)[1]]}
        if path == REPOSITORY + "/actions/runs/123":
            self.run_reads += 1
            return self.final_run if self.run_reads > 1 and self.final_run is not None else self.run
        if path == REPOSITORY + "/actions/runs/123/attempts/1/jobs?per_page=100&page=1":
            return {"jobs": self.jobs, "total_count": len(self.jobs)}
        self.fail(f"Unexpected API call: {path}")

    def invoke(self, command="finish", *, version="0.1.0", sha=LUA_SHA, success=True):
        self.posts, self.requests, self.run_reads = [], [], 0
        error = StringIO()
        args = ["release.py", command, "--version", version, "--sha", sha, "--core-sha", CORE_SHA]
        with patch.dict(os.environ, self.environment, clear=True), patch.object(sys, "argv", args), \
                patch.object(release, "git", side_effect=self.git), \
                patch.object(release, "api", side_effect=self.api), \
                patch.object(release.subprocess, "run") as run, redirect_stderr(error):
            if not self.ancestor:
                run.side_effect = subprocess.CalledProcessError(1, "git merge-base")
            if success:
                release.main()
            else:
                with self.assertRaises(SystemExit) as failure:
                    release.main()
                self.assertEqual(failure.exception.code, 1)
                self.assertEqual(self.posts, [], "Rejected requests must not create a tag")
        return error.getvalue()

    def test_prepare_keeps_numeric_version_and_reports_prefixed_tag(self):
        self.invoke("prepare")
        self.assertEqual(self.posts, [])
        self.assertEqual((self.root / "output").read_text(encoding="utf-8"),
                         f"lua_sha={LUA_SHA}\ncore_sha={CORE_SHA}\nversion=0.1.0\n")
        summary = (self.root / "summary").read_text(encoding="utf-8")
        self.assertIn("- tag: `v0.1.0`", summary)
        self.assertIn("- version: `0.1.0`", summary)
        self.assertIn((REPOSITORY + "/git/ref/tags/v0.1.0", None), self.requests)

    def test_success_creates_only_prefixed_tag_at_exact_candidate(self):
        for version in ("0.0.0", "0.1.0", "12.34.56"):
            with self.subTest(version=version):
                self.invoke(version=version)
                self.assertEqual(self.posts, [(REPOSITORY + "/git/refs",
                                               {"ref": "refs/tags/v" + version, "sha": LUA_SHA})])
                self.assertEqual(self.requests[-1], self.posts[0])
                self.assertEqual(self.run_reads, 2)
        summary = (self.root / "summary").read_text(encoding="utf-8")
        self.assertIn("CTest 15/15; Busted 17; Query 23 fixtures / 41 checks", summary)

    def test_invalid_versions_never_reach_remote(self):
        for version in ("", "v0.1.0", "01.2.3", "1.02.3", "1.2.03", "1.2", "1.2.3.4",
                        "1.2.3-rc.1", "1.2.3+build", " 1.2.3", "1.2.3\n", "1.2.3;echo"):
            with self.subTest(version=version):
                self.invoke(version=version, success=False)
                self.assertEqual(self.requests, [])

    def test_invalid_commit_never_reaches_remote(self):
        for sha in ("main", "a" * 39, "a" * 41, LUA_SHA.upper()):
            with self.subTest(sha=sha):
                self.invoke(sha=sha, success=False)
                self.assertEqual(self.requests, [])

    def test_existing_target_tag_blocks_prepare_and_finish(self):
        self.existing_tag = True
        for command in ("prepare", "finish"):
            with self.subTest(command=command):
                self.assertIn("Release tag already exists", self.invoke(command, success=False))
                self.assertIn((REPOSITORY + "/git/ref/tags/v0.1.0", None), self.requests)

    def test_another_release_tag_on_candidate_blocks_prepare_and_finish(self):
        for annotated in (False, True):
            self.refs = [{"ref": "refs/tags/v0.0.1", "object":
                          {"type": "tag" if annotated else "commit",
                           "sha": OTHER_SHA if annotated else LUA_SHA}}]
            self.annotated_tags = {OTHER_SHA: {"type": "commit", "sha": LUA_SHA}}
            for command in ("prepare", "finish"):
                with self.subTest(annotated=annotated, command=command):
                    self.assertIn("Lua commit already has a release tag", self.invoke(command, success=False))

    def test_unrelated_tags_do_not_count_as_releases(self):
        self.refs = [{"ref": "refs/tags/" + tag, "object": {"type": "commit", "sha": LUA_SHA}}
                     for tag in ("0.1.0", "core-baseline", "v01.2.3", "v1.2.3-rc.1")]
        self.refs.append({"ref": "refs/tags/v0.0.1", "object": {"type": "commit", "sha": OTHER_SHA}})
        self.invoke()
        self.assertEqual(len(self.posts), 1)

    def test_candidate_and_workflow_commit_verification_is_preserved(self):
        for attribute, value in (("git_head", OTHER_SHA), ("git_candidate", OTHER_SHA),
                                 ("ancestor", False), ("core_pin", OTHER_SHA)):
            with self.subTest(attribute=attribute):
                previous = getattr(self, attribute)
                setattr(self, attribute, value)
                self.invoke(success=False)
                setattr(self, attribute, previous)
        self.comparison = {"status": "ahead", "base_commit": {"sha": OTHER_SHA}}
        self.invoke(success=False)

    def test_unsuccessful_required_jobs_block_tag_creation(self):
        for key in ("PREPARE_RESULT", "FULL_CI_RESULT"):
            for result in ("failure", "skipped", "cancelled", ""):
                with self.subTest(key=key, result=result):
                    self.environment[key] = result
                    self.invoke(success=False)
            self.environment[key] = "success"

    def test_every_matrix_job_must_exist_once_and_succeed(self):
        original = successful_jobs()
        for position in range(9):
            for result in ("failure", "skipped", "cancelled", "missing", "duplicate"):
                with self.subTest(position=position, result=result):
                    self.jobs = deepcopy(original)
                    if result == "missing":
                        self.jobs.pop(position)
                    elif result == "duplicate":
                        self.jobs.append(deepcopy(self.jobs[position]))
                    else:
                        self.jobs[position]["conclusion"] = result
                    self.invoke(success=False)

    def test_required_steps_cannot_be_skipped_or_missing(self):
        for position in range(5):
            for mode in ("skipped", "missing"):
                with self.subTest(position=position, mode=mode):
                    self.jobs = successful_jobs()
                    if mode == "missing":
                        self.jobs[0]["steps"].pop(position)
                    else:
                        self.jobs[0]["steps"][position]["conclusion"] = "skipped"
                    self.invoke(success=False)

    def test_run_and_matrix_sha_and_attempt_must_match(self):
        for target in (self.run, self.jobs[0]):
            for key, value in (("head_sha", OTHER_SHA), ("run_attempt", 2)):
                with self.subTest(target=target.get("name", "run"), key=key):
                    previous = target[key]
                    target[key] = value
                    self.invoke(success=False)
                    target[key] = previous

    def test_cancelled_run_is_rejected_immediately_before_tag_creation(self):
        self.final_run = dict(self.run, status="completed", conclusion="cancelled")
        self.assertIn("Release run is no longer active", self.invoke(success=False))
        self.assertEqual(self.run_reads, 2)

    def test_evidence_requires_selected_revisions_and_complete_test_counts(self):
        original = successful_jobs()[0]["steps"][-1]["name"]
        for before, after in ((LUA_SHA, OTHER_SHA), (CORE_SHA, OTHER_SHA),
                              ("CTest=15/15", "CTest=14/15"), ("CTest=15/15", "CTest=14/14"),
                              ("Busted=17", "Busted=0"), ("Query=23/41", "Query=0/41"),
                              ("Query=23/41", "Query=23/0")):
            with self.subTest(before=before, after=after):
                self.jobs[0]["steps"][-1]["name"] = original.replace(before, after)
                self.invoke(success=False)


if __name__ == "__main__":
    unittest.main()
