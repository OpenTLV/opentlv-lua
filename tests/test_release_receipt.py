# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Only completed GitHub publication may create a documentation receipt."""

import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import release


class ReleaseReceiptTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.assets = self.root / "assets"
        self.assets.mkdir()
        self.request = {"commit": "a" * 40, "workflow_sha": "a" * 40, "run_id": "123", "run_attempt": "2",
                        "tag": "v0.1.0", "version": "0.1.0"}
        self.released = {"id": 456, "tag_name": "v0.1.0", "draft": False, "prerelease": False,
                         "published_at": "2026-10-21T12:00:00Z"}
        jobs = [{"name": name, "conclusion": "success", "run_attempt": 2, "head_sha": "a" * 40}
                for name in ("Lua quality gate", "Assemble and attest Lua release")]
        for context in (patch.object(release, "ROOT", self.root),
                        patch.object(release, "verify_payload", return_value=self.request),
                        patch.object(release, "attempt_jobs", return_value=jobs),
                        patch.dict(os.environ, {"FULL_CI_RESULT": "success", "ASSEMBLE_RESULT": "success"})):
            context.start()
            self.addCleanup(context.stop)
        self.calls = []

    def api(self, path, data=None):
        self.calls.append((path, data))
        return {"object": {"sha": "a" * 40}} if data else self.released

    def test_receipt_matches_actual_published_release_and_producer(self):
        with patch.object(release, "api", side_effect=self.api), patch.object(release, "run") as run:
            release.publish(self.assets)
        receipt = json.loads((self.root / "build/docs-release/release.json").read_text())
        self.assertEqual(receipt, {"schema": 1, "kind": "component-release", "status": "published", "source": "lua",
            "repository": release.REPOSITORY, "commit": "a" * 40, "workflow_commit": "a" * 40,
            "tag": "v0.1.0", "version": "0.1.0", "run_id": 123, "run_attempt": 2, "release_id": 456})
        self.assertEqual(run.call_args.args[:3], ("gh", "release", "create"))

    def test_failed_publication_never_emits_receipt(self):
        with patch.object(release, "api", side_effect=self.api), \
                patch.object(release, "run", side_effect=RuntimeError("upload failed")), self.assertRaises(RuntimeError):
            release.publish(self.assets)
        self.assertFalse((self.root / "build/docs-release/release.json").exists())

    def test_release_titles_use_version_while_git_refs_keep_v(self):
        for version in ('0.3.0', '12.34.56'):
            tag = 'v' + version
            with self.subTest(version=version), patch.dict(self.request, version=version, tag=tag), \
                    patch.dict(self.released, tag_name=tag), patch.object(release, 'api', side_effect=self.api), \
                    patch.object(release, 'run') as run:
                release.publish(self.assets)
                run.assert_called_once_with('gh', 'release', 'create', tag, '--repo', release.REPOSITORY,
                                            '--verify-tag', '--title', 'OpenTLV Lua ' + version, '--generate-notes')
                self.assertIn((release.REPOSITORY + '/git/refs',
                               {'ref': 'refs/tags/' + tag, 'sha': self.request['commit']}), self.calls)

    def test_draft_or_mismatched_release_never_emits_receipt(self):
        for change in ({"draft": True}, {"id": True}, {"published_at": None}, {"tag_name": "v0.2.0"}):
            with patch.dict(self.released, change), patch.object(release, "api", side_effect=self.api), \
                    patch.object(release, "run"), self.assertRaises(ValueError):
                release.publish(self.assets)
            self.assertFalse((self.root / "build/docs-release/release.json").exists())


if __name__ == "__main__":
    unittest.main()
