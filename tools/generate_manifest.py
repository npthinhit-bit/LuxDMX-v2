#!/usr/bin/env python3
"""Materialize an explicit input set and emit a deterministic generated manifest."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

CHUNK_BYTES = 1024 * 1024
MAX_INPUT_BYTES = 64 * 1024 * 1024
MAX_TOTAL_BYTES = 256 * 1024 * 1024
SCHEMA = "luxdmx.generated-manifest.v1"
GENERATOR_NAME = "luxdmx.identity-generator"


class GeneratorError(ValueError):
    """A deterministic, user-actionable generator input or output error."""


def sha256_copy(source: Path, destination: Path) -> tuple[int, str]:
    """Copy bytes while hashing both the source stream and exact output stream."""
    size = source.stat().st_size
    if size > MAX_INPUT_BYTES:
        raise GeneratorError(
            f"input exceeds {MAX_INPUT_BYTES} byte safety bound: {source}"
        )

    digest = hashlib.sha256()
    written = 0
    with source.open("rb") as source_stream, destination.open("wb") as output_stream:
        while chunk := source_stream.read(CHUNK_BYTES):
            digest.update(chunk)
            output_stream.write(chunk)
            written += len(chunk)
    if written != size:
        raise GeneratorError(f"input changed during read: {source}")
    os.chmod(destination, 0o644)
    return written, digest.hexdigest()


def normalized_relative_input(input_root: Path, raw_path: str) -> tuple[Path, str]:
    """Resolve one explicit input and return its stable POSIX relative path."""
    if not raw_path or "\\" in raw_path:
        raise GeneratorError(f"input path must be a non-empty POSIX relative path: {raw_path!r}")
    candidate = Path(raw_path)
    if candidate.is_absolute():
        raise GeneratorError(f"input path must be relative to --input-root: {raw_path}")

    lexical = PurePosixPath(raw_path)
    if any(part in ("", ".", "..") for part in lexical.parts):
        raise GeneratorError(f"input path contains non-canonical components: {raw_path}")
    relative = lexical.as_posix()
    path = input_root / Path(*lexical.parts)
    if path.is_symlink():
        raise GeneratorError(f"symlink input is not allowed: {raw_path}")
    resolved = path.resolve(strict=False)
    try:
        resolved.relative_to(input_root)
    except ValueError as exc:
        raise GeneratorError(f"input resolves outside --input-root: {raw_path}") from exc
    if not path.is_file():
        raise GeneratorError(f"input file not found: {raw_path}")
    return path, relative


def validate_manifest_name(raw_name: str) -> str:
    """Validate a manifest name that is relative to the generated output directory."""
    name = PurePosixPath(raw_name)
    if len(name.parts) != 1 or name.name in ("", ".", "..") or name.suffix != ".json":
        raise GeneratorError(
            f"manifest name must be one relative .json filename: {raw_name}"
        )
    return name.name


def stable_entries(input_root: Path, raw_inputs: Iterable[str]) -> list[tuple[Path, str]]:
    entries = [normalized_relative_input(input_root, raw) for raw in raw_inputs]
    entries.sort(key=lambda item: item[1].encode("utf-8"))
    seen: set[str] = set()
    for _, relative in entries:
        if relative in seen:
            raise GeneratorError(f"duplicate normalized input path: {relative}")
        seen.add(relative)
    total = sum(path.stat().st_size for path, _ in entries)
    if total > MAX_TOTAL_BYTES:
        raise GeneratorError(
            f"inputs exceed {MAX_TOTAL_BYTES} byte total safety bound: {total}"
        )
    return entries


def build_manifest(
    input_root: Path,
    output_dir: Path,
    entries: list[tuple[Path, str]],
    generator_version: str,
    manifest_name: str,
) -> dict[str, Any]:
    """Copy inputs into a staged directory and return a stable manifest object."""
    input_records: list[dict[str, Any]] = []
    output_records: list[dict[str, Any]] = []
    for source, relative in entries:
        destination = output_dir / Path(*PurePosixPath(relative).parts)
        destination.parent.mkdir(parents=True, exist_ok=True)
        size, digest = sha256_copy(source, destination)
        input_records.append(
            {"path": relative, "bytes": size, "sha256": digest}
        )
        output_records.append(
            {
                "path": relative,
                "source": relative,
                "bytes": size,
                "sha256": digest,
            }
        )

    return {
        "schema": SCHEMA,
        "generator": {"name": GENERATOR_NAME, "version": generator_version},
        "inputs": input_records,
        "outputs": output_records,
        "manifest": {"path": manifest_name},
        "status": "pass",
        "errors": [],
    }


def write_manifest_atomic(output_dir: Path, manifest_name: str, manifest: dict[str, Any]) -> None:
    manifest_path = output_dir / manifest_name
    manifest_path.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    os.chmod(manifest_path, 0o644)


def generate(
    input_root: Path,
    raw_inputs: list[str],
    output_dir: Path,
    manifest_name: str = "generated-manifest.json",
    generator_version: str = "1",
) -> dict[str, Any]:
    """Run the generator with an atomic output-directory commit."""
    input_root = input_root.resolve()
    if not input_root.is_dir():
        raise GeneratorError(f"input root not found: {input_root}")
    manifest_name = validate_manifest_name(manifest_name)
    entries = stable_entries(input_root, raw_inputs)
    output_dir = output_dir.resolve()
    if output_dir.exists():
        raise GeneratorError(f"output directory already exists; refusing stale output: {output_dir}")
    output_dir.parent.mkdir(parents=True, exist_ok=True)

    staging = Path(
        tempfile.mkdtemp(prefix=f".{output_dir.name}.staging-", dir=output_dir.parent)
    )
    try:
        manifest = build_manifest(input_root, staging, entries, generator_version, manifest_name)
        write_manifest_atomic(staging, manifest_name, manifest)
        os.replace(staging, output_dir)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return manifest


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-root", type=Path, required=True)
    parser.add_argument(
        "--input",
        dest="inputs",
        action="append",
        required=True,
        help="POSIX relative input path; repeat for each explicit input",
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--manifest-name", default="generated-manifest.json")
    parser.add_argument("--generator-version", default="1")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        manifest = generate(
            input_root=args.input_root,
            raw_inputs=args.inputs,
            output_dir=args.output_dir,
            manifest_name=args.manifest_name,
            generator_version=args.generator_version,
        )
    except (GeneratorError, OSError) as exc:
        print(f"Generated manifest: fail: {exc}", file=sys.stderr)
        return 1
    print(
        f"Generated manifest: pass ({args.output_dir / args.manifest_name}; "
        f"{len(manifest['outputs'])} outputs)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
