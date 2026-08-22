#!/usr/bin/env python3
"""Tests for tools/repository_hygiene.py."""
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from repository_hygiene import (
    find_tracked_violations,
    find_worktree_violations,
    normalize_paths,
)


class RepositoryHygieneTests(unittest.TestCase):
    def test_normalizes_paths_deterministically(self) -> None:
        self.assertEqual(
            normalize_paths(["./src/a.c", "src\\b.c", "src/a.c"]),
            ["src/a.c", "src/b.c"],
        )

    def test_rejects_tracked_generated_and_secret_files(self) -> None:
        violations = find_tracked_violations(
            [
                ".pio/build/firmware.bin",
                "test/build/CMakeCache.txt",
                "sdkconfig.esp32dev",
                "release/private-key.pem",
                "firmware.elf",
            ]
        )
        self.assertEqual(len(violations), 5)
        self.assertTrue(any("generated build directory" in item for item in violations))
        self.assertTrue(any("generated sdkconfig" in item for item in violations))
        self.assertTrue(any("private-key" in item for item in violations))

    def test_allows_tracked_source_of_truth_defaults(self) -> None:
        self.assertEqual(
            find_tracked_violations(
                [
                    "sdkconfig.defaults",
                    "sdkconfig.defaults.esp32dev",
                    "platformio.ini",
                    "tools/repository_hygiene.py",
                ]
            ),
            [],
        )

    def test_rejects_workspace_outputs_but_not_source_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / ".pio" / "build").mkdir(parents=True)
            (root / ".pio" / "build" / "firmware.bin").write_bytes(b"bin")
            (root / "test" / "build").mkdir(parents=True)
            (root / "sdkconfig.esp32dev").write_text("CONFIG_TEST=y\n")
            (root / "components" / "demo.c").parent.mkdir()
            (root / "components" / "demo.c").write_text("int demo;\n")

            violations = find_worktree_violations(root)
            self.assertTrue(any(".pio: generated build directory" in item for item in violations))
            self.assertTrue(any("test/build: generated build directory" in item for item in violations))
            self.assertTrue(any("sdkconfig.esp32dev" in item for item in violations))
            self.assertFalse(any("components/demo.c" in item for item in violations))


if __name__ == "__main__":
    unittest.main()
