#!/usr/bin/env python3
"""Tests for tools/firmware_artifact_report.py."""
from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from firmware_artifact_report import build_report


class FirmwareArtifactReportTests(unittest.TestCase):
    def setUp(self) -> None:
        self.repo_root = Path(__file__).resolve().parent.parent

    def write_ini(self, directory: Path) -> Path:
        ini = directory / "platformio.ini"
        ini.write_text(
            "[env:base]\n"
            "platform = test-platform\n"
            "framework = test-framework\n"
            "board = test-board\n"
            "[env:demo_release]\n"
            "extends = env:base\n"
            "board = demo-board\n",
            encoding="utf-8",
        )
        return ini

    def test_reports_hashes_and_resolves_release_profile(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            build_dir = root / "build"
            build_dir.mkdir()
            data = {
                "firmware.bin": b"firmware",
                "partitions.bin": b"partitions",
                "bootloader.bin": b"bootloader",
                "firmware.elf": b"elf",
            }
            for name, content in data.items():
                (build_dir / name).write_bytes(content)

            report, errors = build_report(
                self.repo_root, "demo_release", build_dir, self.write_ini(root)
            )

            self.assertEqual(errors, [])
            self.assertEqual(report["status"], "pass")
            self.assertEqual(report["profile"], "release")
            self.assertEqual(report["board"], "demo-board")
            self.assertEqual(report["framework"], "test-framework")
            self.assertEqual(report["artifacts"]["firmware.bin"]["bytes"], 8)
            self.assertEqual(
                report["artifacts"]["firmware.bin"]["sha256"],
                hashlib.sha256(b"firmware").hexdigest(),
            )
            json.dumps(report, sort_keys=True)

    def test_missing_required_artifact_is_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            build_dir = root / "build"
            build_dir.mkdir()
            for name in ("firmware.bin", "partitions.bin"):
                (build_dir / name).write_bytes(b"x")

            report, errors = build_report(
                self.repo_root, "demo", build_dir, self.write_ini(root)
            )

            self.assertEqual(report["status"], "fail")
            self.assertTrue(any("missing required artifact" in error for error in errors))
            self.assertEqual(report["artifacts"]["bootloader.bin"]["present"], False)

    def test_rejects_file_over_safety_bound(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            build_dir = root / "build"
            build_dir.mkdir()
            for name in ("firmware.bin", "partitions.bin", "bootloader.bin"):
                (build_dir / name).write_bytes(b"x")
            oversized = build_dir / "firmware.elf"
            oversized.write_bytes(b"oversized")

            with patch("firmware_artifact_report.MAX_INPUT_BYTES", 4):
                report, errors = build_report(
                    self.repo_root, "demo", build_dir, self.write_ini(root)
                )

            self.assertEqual(report["status"], "fail")
            self.assertTrue(any("safety bound" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
