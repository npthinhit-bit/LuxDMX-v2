#!/usr/bin/env python3
"""Tests for tools/generate_manifest.py."""
from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from generate_manifest import GeneratorError, generate


class GenerateManifestTests(unittest.TestCase):
    def write_inputs(self, root: Path) -> Path:
        input_root = root / "inputs"
        (input_root / "nested").mkdir(parents=True)
        (input_root / "alpha.txt").write_text("alpha\n", encoding="utf-8")
        (input_root / "nested" / "binary.bin").write_bytes(b"\x00\x01\xff\n")
        return input_root

    def test_manifest_and_outputs_are_byte_identical_across_runs(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            input_root = self.write_inputs(root)
            first = generate(
                input_root,
                ["nested/binary.bin", "alpha.txt"],
                root / "out-one",
            )
            second = generate(
                input_root,
                ["alpha.txt", "nested/binary.bin"],
                root / "out-two",
            )

            self.assertEqual(first, second)
            self.assertEqual(first["schema"], "luxdmx.generated-manifest.v1")
            self.assertEqual(first["status"], "pass")
            self.assertEqual(
                [entry["path"] for entry in first["inputs"]],
                ["alpha.txt", "nested/binary.bin"],
            )
            self.assertEqual(
                first["outputs"][1]["sha256"],
                hashlib.sha256(b"\x00\x01\xff\n").hexdigest(),
            )

            first_files = sorted(
                path.relative_to(root / "out-one").as_posix()
                for path in (root / "out-one").rglob("*")
                if path.is_file()
            )
            second_files = sorted(
                path.relative_to(root / "out-two").as_posix()
                for path in (root / "out-two").rglob("*")
                if path.is_file()
            )
            self.assertEqual(first_files, second_files)
            for relative in first_files:
                self.assertEqual(
                    (root / "out-one" / relative).read_bytes(),
                    (root / "out-two" / relative).read_bytes(),
                )
            self.assertEqual(
                (root / "out-one" / "generated-manifest.json").read_bytes(),
                (root / "out-two" / "generated-manifest.json").read_bytes(),
            )
            json.loads(
                (root / "out-one" / "generated-manifest.json").read_text(
                    encoding="utf-8"
                )
            )

    def test_rejects_missing_input_without_creating_output(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            input_root = self.write_inputs(root)
            output = root / "missing-output"
            with self.assertRaisesRegex(GeneratorError, "input file not found"):
                generate(input_root, ["missing.txt"], output)
            self.assertFalse(output.exists())

    def test_rejects_noncanonical_and_duplicate_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            input_root = self.write_inputs(root)
            with self.assertRaisesRegex(GeneratorError, "non-canonical"):
                generate(input_root, ["nested/../alpha.txt"], root / "escape")
            with self.assertRaisesRegex(GeneratorError, "duplicate normalized"):
                generate(input_root, ["alpha.txt", "alpha.txt"], root / "duplicate")
            self.assertFalse((root / "escape").exists())
            self.assertFalse((root / "duplicate").exists())

    def test_rejects_symlink_input(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            input_root = self.write_inputs(root)
            link = input_root / "link.txt"
            try:
                link.symlink_to(input_root / "alpha.txt")
            except (NotImplementedError, OSError):
                self.skipTest("symlinks are unavailable")
            with self.assertRaisesRegex(GeneratorError, "symlink"):
                generate(input_root, ["link.txt"], root / "symlink")

    def test_rejects_safety_bound_and_existing_output(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            input_root = self.write_inputs(root)
            with patch("generate_manifest.MAX_INPUT_BYTES", 2):
                with self.assertRaisesRegex(GeneratorError, "safety bound"):
                    generate(input_root, ["alpha.txt"], root / "oversized")
            existing = root / "existing"
            existing.mkdir()
            with self.assertRaisesRegex(GeneratorError, "already exists"):
                generate(input_root, ["alpha.txt"], existing)

    def test_rejects_absolute_and_backslash_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            input_root = self.write_inputs(root)
            with self.assertRaisesRegex(GeneratorError, "must be relative"):
                generate(input_root, [str(input_root / "alpha.txt")], root / "absolute")
            with self.assertRaisesRegex(GeneratorError, "POSIX"):
                generate(input_root, ["nested\\binary.bin"], root / "backslash")


if __name__ == "__main__":
    unittest.main()
