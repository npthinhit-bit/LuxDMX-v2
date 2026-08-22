#!/usr/bin/env python3
"""Tests for tools/ci_capture_context.py."""
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from ci_capture_context import capture_context


class CiCaptureContextTests(unittest.TestCase):
    def test_writes_allowlisted_context_files(self) -> None:
        repo_root = Path(__file__).resolve().parent.parent
        with tempfile.TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "artifacts" / "native-unit"
            capture_context(repo_root, output_dir, "native-unit")
            expected = {
                "label.txt",
                "platform.txt",
                "git-revision.txt",
                "git-status.txt",
                "python-version.txt",
                "cmake-version.txt",
                "ctest-version.txt",
                "environment-policy.txt",
            }
            self.assertEqual({item.name for item in output_dir.iterdir()}, expected)
            self.assertEqual(
                (output_dir / "label.txt").read_text(encoding="utf-8"),
                "native-unit\n",
            )

    def test_policy_file_explicitly_excludes_environment_dump(self) -> None:
        repo_root = Path(__file__).resolve().parent.parent
        with tempfile.TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir)
            capture_context(repo_root, output_dir, "security-check")
            policy = (output_dir / "environment-policy.txt").read_text(
                encoding="utf-8"
            )
            self.assertIn("not captured", policy)
            self.assertFalse((output_dir / "environment.txt").exists())


if __name__ == "__main__":
    unittest.main()
