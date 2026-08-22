#!/usr/bin/env python3
"""Tests for tools/template_generator.py."""
from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from template_generator import GeneratorError, generate_templates


class TemplateGeneratorTests(unittest.TestCase):
    def write(self, root: Path, relative: str, content: str | bytes) -> None:
        path = root.joinpath(*Path(relative).parts)
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(content, bytes):
            path.write_bytes(content)
        else:
            path.write_text(content, encoding="utf-8")

    def test_resolves_base_first_and_emits_golden_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self.write(root, "_base.ini", "# base\nalpha=one\nshared=base\n")
            self.write(
                root,
                "board.ini",
                "extends=_base\nshared=child\nbeta=two\n",
            )
            expected = b"alpha=one\nshared=child\nbeta=two\n"
            manifest = generate_templates(root, ["board.ini"], root / "out")
            output = root / "out" / "board.ini"
            self.assertEqual(output.read_bytes(), expected)
            self.assertEqual(manifest["status"], "pass")
            self.assertEqual(manifest["templates"][0]["inheritance"], ["_base.ini", "board.ini"])
            self.assertEqual(
                [entry["path"] for entry in manifest["inputs"]],
                ["_base.ini", "board.ini"],
            )
            self.assertEqual(
                manifest["outputs"][0]["sha256"], hashlib.sha256(expected).hexdigest()
            )
            self.assertEqual(
                json.loads((root / "out" / "generated-manifest.json").read_text()),
                manifest,
            )

    def test_root_order_does_not_change_output_tree_or_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self.write(root, "_base.ini", "base=1\n")
            self.write(root, "a.ini", "extends=_base\na=1\n")
            self.write(root, "b.ini", "extends=_base\nb=2\n")
            first = generate_templates(root, ["b.ini", "a.ini"], root / "one")
            second = generate_templates(root, ["a.ini", "b.ini"], root / "two")
            self.assertEqual(first, second)
            for relative in ("a.ini", "b.ini", "generated-manifest.json"):
                self.assertEqual(
                    (root / "one" / relative).read_bytes(),
                    (root / "two" / relative).read_bytes(),
                )

    def test_rejects_missing_parent_and_cycle(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self.write(root, "child.ini", "extends=missing\nkey=value\n")
            with self.assertRaisesRegex(GeneratorError, "template file not found"):
                generate_templates(root, ["child.ini"], root / "missing")
            self.write(root, "a.ini", "extends=b\na=1\n")
            self.write(root, "b.ini", "extends=a\nb=2\n")
            with self.assertRaisesRegex(GeneratorError, "inheritance cycle"):
                generate_templates(root, ["a.ini"], root / "cycle")
            self.assertFalse((root / "missing").exists())
            self.assertFalse((root / "cycle").exists())

    def test_rejects_duplicate_directives_keys_and_malformed_lines(self) -> None:
        cases = {
            "duplicate-extends.ini": "extends=_base\nextends=_base\n",
            "duplicate-key.ini": "key=one\nkey=two\n",
            "malformed.ini": "not-a-key-value-line\n",
            "empty-key.ini": "=value\n",
        }
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self.write(root, "_base.ini", "base=1\n")
            expected_errors = {
                "duplicate-extends.ini": "duplicate extends",
                "duplicate-key.ini": "duplicate template key",
                "malformed.ini": "malformed template line",
                "empty-key.ini": "empty template key",
            }
            for filename, content in cases.items():
                self.write(root, filename, content)
                with self.assertRaisesRegex(GeneratorError, expected_errors[filename]):
                    generate_templates(root, [filename], root / f"{filename}-out")

    def test_rejects_depth_invalid_utf8_and_unsafe_size(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self.write(root, "_base.ini", "base=1\n")
            self.write(root, "child.ini", "extends=_base\nchild=1\n")
            with self.assertRaisesRegex(GeneratorError, "exceeds depth 1"):
                generate_templates(root, ["child.ini"], root / "depth", max_depth=1)
            self.write(root, "invalid.ini", b"key=\xff\n")
            with self.assertRaisesRegex(GeneratorError, "not valid UTF-8"):
                generate_templates(root, ["invalid.ini"], root / "utf8")
            self.write(root, "large.ini", "large=value\n")
            with patch("template_generator.MAX_INPUT_BYTES", 2):
                with self.assertRaisesRegex(GeneratorError, "safety bound"):
                    generate_templates(root, ["large.ini"], root / "large")

    def test_rejects_noncanonical_paths_and_existing_output(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self.write(root, "_base.ini", "base=1\n")
            with self.assertRaisesRegex(GeneratorError, "canonical and relative"):
                generate_templates(root, ["nested/../_base.ini"], root / "noncanonical")
            existing = root / "existing"
            existing.mkdir()
            with self.assertRaisesRegex(GeneratorError, "already exists"):
                generate_templates(root, ["_base.ini"], existing)


if __name__ == "__main__":
    unittest.main()
