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

    def metadata(self, source=None, success=True, release=None, error="Multiple Lua release tags"):
        command = ["cmake", "-DLUA_SOURCE=" + str(source or self.source),
                   "-DLUA_BINARY=" + str(self.root / "output")]
        if release is not None:
            command.append("-DOPENTLV_LUA_RELEASE_TAG=" + release)
        result = subprocess.run(command + ["-P", str(SCRIPT)], capture_output=True, text=True)
        if not success:
            self.assertNotEqual(result.returncode, 0)
            # CMake wraps long messages.
            self.assertIn(error, " ".join(result.stderr.split()))
            return
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        metadata = json.loads((self.root / "output/lua-version.json").read_text(encoding="utf-8"))
        header = (self.root / "output/generated/lua-version.h").read_text(encoding="utf-8")
        self.assertIn('#define OPENTLV_LUA_VERSION "' + metadata["binding_version"] + '"', header)
        return metadata

    def test_unchanged_version_preserves_header_timestamp(self):
        self.metadata()
        header = self.root / "output/generated/lua-version.h"
        before = header.stat().st_mtime_ns
        self.metadata()
        self.assertEqual(header.stat().st_mtime_ns, before)

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
        self.assertEqual(self.metadata(archive, release="")["binding_version"], "scm+unknown")

    def test_versioned_rockspec_declares_release_without_git(self):
        # LuaRocks deletes .git from the tag a versioned rockspec clones.
        archive = self.root / "archive"
        archive.mkdir()
        self.assertEqual(self.metadata(archive, release="v0.3.0"), {
            "binding_version": "0.3.0", "binding_revision": "",
            "release_tag": "v0.3.0", "dirty": False})
        nested = self.source / "nested"
        nested.mkdir()
        self.git("tag", "v9.8.7")
        self.assertEqual(self.metadata(nested, release="v0.3.0")["release_tag"], "v0.3.0")

    def test_declared_release_must_match_git_checkout(self):
        self.metadata(release="v0.3.0", success=False,
                      error="builds Lua release v0.3.0, but the Lua checkout reports scm+g" + self.sha[:12])
        self.git("tag", "v0.2.0")
        self.metadata(release="v0.3.0", success=False, error="but the Lua checkout reports 0.2.0")
        self.git("tag", "-d", "v0.2.0")
        self.git("tag", "v0.3.0")
        self.assertEqual(self.metadata(release="v0.3.0"), {
            "binding_version": "0.3.0", "binding_revision": self.sha,
            "release_tag": "v0.3.0", "dirty": False})
        (self.source / "tracked").write_text("modified\n", encoding="utf-8")
        self.assertEqual(self.metadata(release="v0.3.0")["binding_version"],
                         "0.3.0+g" + self.sha[:12] + ".dirty")

    def test_invalid_declared_release_rejected(self):
        archive = self.root / "archive"
        archive.mkdir()
        for release in ("0.3.0", "v03.0.0", "v0.3", "v0.3.0-rc.1", "ON"):
            with self.subTest(release=release):
                self.metadata(archive, release=release, success=False,
                              error="must be a vMAJOR.MINOR.PATCH Lua release tag")


if __name__ == "__main__":
    unittest.main()
