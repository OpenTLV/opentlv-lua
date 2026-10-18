#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Check release identity using disposable Git fixtures, never the real tags."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "cmake/GenerateVersion.cmake"


class VersionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="lua version ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.git("init")
        self.git("config", "user.name", "Version fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        self.git("config", "commit.gpgsign", "false")
        self.git("config", "tag.gpgsign", "false")
        (self.source / "tracked").write_text("baseline\n", encoding="utf-8")
        self.git("add", "tracked")
        self.git("-c", "core.hooksPath=", "commit", "-m", "fixture")
        self.sha = self.git("rev-parse", "HEAD")

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.source), *args],
                                       text=True, stderr=subprocess.STDOUT).strip()

    def metadata(self, source=None, success=True):
        result = subprocess.run(["cmake", "-DLUA_SOURCE=" + str(source or self.source),
                                 "-DLUA_BINARY=" + str(self.root / "output"), "-P", str(SCRIPT)],
                                capture_output=True, text=True)
        if not success:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Multiple Lua release tags", result.stderr)
            return
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return json.loads((self.root / "output/lua-version.json").read_text(encoding="utf-8"))

    def test_untagged_and_unrelated_tag(self):
        for tag in ("core-baseline", "0.1.0", "v01.2.3", "v1.02.3", "v1.2.03",
                    "v1.2", "v1.2.3-rc.1", "v1.2.3+build"):
            self.git("tag", tag)
        self.assertEqual(self.metadata(), {
            "binding_version": "scm+g" + self.sha[:12],
            "binding_revision": self.sha, "release_tag": "", "dirty": False})

    def test_release_and_next_commit(self):
        self.git("tag", "-a", "v0.1.0", "-m", "release fixture")
        self.assertEqual(self.metadata(), {
            "binding_version": "0.1.0", "binding_revision": self.sha,
            "release_tag": "v0.1.0", "dirty": False})
        (self.source / "tracked").write_text("next\n", encoding="utf-8")
        self.git("add", "tracked")
        self.git("-c", "core.hooksPath=", "commit", "-m", "next fixture")
        revision = self.git("rev-parse", "HEAD")
        self.assertEqual(self.metadata(), {
            "binding_version": "scm+g" + revision[:12],
            "binding_revision": revision, "release_tag": "", "dirty": False})

    def test_lightweight_release_and_numeric_tag(self):
        self.git("tag", "v12.34.56")
        self.git("tag", "0.1.0")
        self.assertEqual(self.metadata(), {
            "binding_version": "12.34.56", "binding_revision": self.sha,
            "release_tag": "v12.34.56", "dirty": False})

    def test_dirty_release_and_untracked_source(self):
        self.git("tag", "v0.1.0")
        (self.source / "tracked").write_text("modified\n", encoding="utf-8")
        expected = {"binding_version": "0.1.0+g" + self.sha[:12] + ".dirty",
                    "binding_revision": self.sha, "release_tag": "v0.1.0", "dirty": True}
        self.assertEqual(self.metadata(), expected)
        self.git("checkout", "--", "tracked")
        (self.source / "untracked").write_text("new source\n", encoding="utf-8")
        self.assertEqual(self.metadata(), expected)

    def test_dirty_development_version(self):
        (self.source / "tracked").write_text("modified\n", encoding="utf-8")
        self.assertEqual(self.metadata(), {
            "binding_version": "scm+g" + self.sha[:12] + ".dirty",
            "binding_revision": self.sha, "release_tag": "", "dirty": True})

    def test_ambiguous_release_tags_rejected(self):
        self.git("tag", "v0.1.0")
        self.git("tag", "-a", "v0.2.0", "-m", "second release fixture")
        self.metadata(success=False)

    def test_ignored_build_and_runtime_tools_keep_release_clean(self):
        (self.source / ".gitignore").write_text("/build/\n/.lua/\n/.luarocks/\n", encoding="utf-8")
        self.git("add", ".gitignore")
        self.git("-c", "core.hooksPath=", "commit", "-m", "ignore generated tools")
        self.git("tag", "v0.1.0")
        for name in ("build", ".lua", ".luarocks"):
            directory = self.source / name
            directory.mkdir()
            (directory / "generated").write_text("generated\n", encoding="utf-8")
        self.assertEqual(self.metadata()["binding_version"], "0.1.0")
        self.assertFalse(self.metadata()["dirty"])

    def test_archive_does_not_inherit_parent_version(self):
        self.git("tag", "v9.8.7")
        archive = self.source / "archive"
        archive.mkdir()
        self.assertEqual(self.metadata(archive), {
            "binding_version": "scm+unknown", "binding_revision": "",
            "release_tag": "", "dirty": False})

    def test_archive_without_git(self):
        archive = self.root / "archive"
        archive.mkdir()
        self.assertEqual(self.metadata(archive)["binding_version"], "scm+unknown")


if __name__ == "__main__":
    unittest.main()
