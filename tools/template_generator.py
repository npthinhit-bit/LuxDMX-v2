#!/usr/bin/env python3
"""Resolve explicit .ini templates and emit deterministic generated templates."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any

from generate_manifest import CHUNK_BYTES, GeneratorError, MAX_INPUT_BYTES, MAX_TOTAL_BYTES

SCHEMA = "luxdmx.generated-manifest.v1"
GENERATOR_NAME = "luxdmx.template-generator"
DEFAULT_MAX_DEPTH = 8
MAX_OUTPUT_BYTES = 64 * 1024 * 1024


@dataclass(frozen=True)
class TemplateDocument:
    relative: str
    parent: str | None
    values: tuple[tuple[str, str], ...]


def sha256_bytes(data: bytes) -> str:
    digest = hashlib.sha256()
    digest.update(data)
    return digest.hexdigest()


def file_record(path: Path, relative: str) -> dict[str, Any]:
    size = path.stat().st_size
    if size > MAX_INPUT_BYTES:
        raise GeneratorError(
            f"template exceeds {MAX_INPUT_BYTES} byte safety bound: {relative}"
        )
    digest = hashlib.sha256()
    read_size = 0
    with path.open("rb") as stream:
        while chunk := stream.read(CHUNK_BYTES):
            digest.update(chunk)
            read_size += len(chunk)
    if read_size != size:
        raise GeneratorError(f"template changed during read: {relative}")
    return {"path": relative, "bytes": size, "sha256": digest.hexdigest()}


def validate_relative_ini(raw_path: str) -> str:
    if not raw_path or "\\" in raw_path:
        raise GeneratorError(f"template path must be a non-empty POSIX path: {raw_path!r}")
    lexical = PurePosixPath(raw_path)
    if lexical.is_absolute() or any(part in ("", ".", "..") for part in lexical.parts):
        raise GeneratorError(f"template path must be canonical and relative: {raw_path}")
    relative = lexical.as_posix()
    if not relative.endswith(".ini"):
        raise GeneratorError(f"template path must use .ini extension: {raw_path}")
    return relative


def resolve_parent_name(raw_parent: str, child_relative: str) -> str:
    parent = raw_parent.strip()
    if not parent:
        raise GeneratorError(f"empty extends target in template: {child_relative}")
    if not parent.endswith(".ini"):
        parent = f"{parent}.ini"
    try:
        return validate_relative_ini(parent)
    except GeneratorError as exc:
        raise GeneratorError(
            f"invalid extends target {raw_parent!r} in template: {child_relative}"
        ) from exc


def parse_template(path: Path, relative: str) -> TemplateDocument:
    try:
        data = path.read_bytes()
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise GeneratorError(f"template is not valid UTF-8: {relative}") from exc
    if len(data) > MAX_INPUT_BYTES:
        raise GeneratorError(
            f"template exceeds {MAX_INPUT_BYTES} byte safety bound: {relative}"
        )

    parent: str | None = None
    values: list[tuple[str, str]] = []
    seen_keys: set[str] = set()
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise GeneratorError(f"malformed template line {relative}:{line_number}")
        raw_key, raw_value = line.split("=", 1)
        key = raw_key.strip()
        value = raw_value.strip()
        if not key:
            raise GeneratorError(f"empty template key {relative}:{line_number}")
        if key == "extends":
            if parent is not None:
                raise GeneratorError(f"duplicate extends directive in template: {relative}")
            parent = resolve_parent_name(value, relative)
            continue
        if key in seen_keys:
            raise GeneratorError(f"duplicate template key {key!r} in template: {relative}")
        seen_keys.add(key)
        values.append((key, value))
    return TemplateDocument(relative=relative, parent=parent, values=tuple(values))


def template_path(input_root: Path, relative: str) -> Path:
    path = input_root.joinpath(*PurePosixPath(relative).parts)
    if path.is_symlink():
        raise GeneratorError(f"symlink template is not allowed: {relative}")
    resolved = path.resolve(strict=False)
    try:
        resolved.relative_to(input_root)
    except ValueError as exc:
        raise GeneratorError(f"template resolves outside --input-root: {relative}") from exc
    if not path.is_file():
        raise GeneratorError(f"template file not found: {relative}")
    return path


def resolve_template(
    input_root: Path,
    relative: str,
    max_depth: int,
    stack: tuple[str, ...] = (),
    cache: dict[str, tuple[TemplateDocument, tuple[str, ...], tuple[tuple[str, str], ...]]] | None = None,
) -> tuple[TemplateDocument, tuple[str, ...], tuple[tuple[str, str], ...]]:
    if cache is None:
        cache = {}
    if relative in stack:
        cycle = " -> ".join((*stack, relative))
        raise GeneratorError(f"template inheritance cycle: {cycle}")
    if len(stack) >= max_depth:
        chain = " -> ".join((*stack, relative))
        raise GeneratorError(f"template inheritance exceeds depth {max_depth}: {chain}")
    if relative in cache:
        return cache[relative]

    document = parse_template(template_path(input_root, relative), relative)
    merged: dict[str, str] = {}
    chain: tuple[str, ...] = (relative,)
    if document.parent is not None:
        parent_doc, parent_chain, parent_values = resolve_template(
            input_root, document.parent, max_depth, (*stack, relative), cache
        )
        del parent_doc
        chain = (*parent_chain, relative)
        merged.update(parent_values)
    for key, value in document.values:
        merged[key] = value
    resolved = (document, chain, tuple(merged.items()))
    cache[relative] = resolved
    return resolved


def resolved_bytes(values: tuple[tuple[str, str], ...]) -> bytes:
    data = "".join(f"{key}={value}\n" for key, value in values).encode("utf-8")
    if len(data) > MAX_OUTPUT_BYTES:
        raise GeneratorError(
            f"resolved template exceeds {MAX_OUTPUT_BYTES} byte safety bound"
        )
    return data


def validate_manifest_name(raw_name: str) -> str:
    name = PurePosixPath(raw_name)
    if len(name.parts) != 1 or name.name in ("", ".", "..") or name.suffix != ".json":
        raise GeneratorError(
            f"manifest name must be one relative .json filename: {raw_name}"
        )
    return name.name


def generate_templates(
    input_root: Path,
    raw_templates: list[str],
    output_dir: Path,
    manifest_name: str = "generated-manifest.json",
    generator_version: str = "1",
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> dict[str, Any]:
    if max_depth < 1:
        raise GeneratorError("max depth must be at least 1")
    input_root = input_root.resolve()
    if not input_root.is_dir():
        raise GeneratorError(f"input root not found: {input_root}")
    roots = sorted({validate_relative_ini(raw) for raw in raw_templates}, key=lambda p: p.encode())
    if not roots:
        raise GeneratorError("at least one template is required")
    manifest_name = validate_manifest_name(manifest_name)
    output_dir = output_dir.resolve()
    if output_dir.exists():
        raise GeneratorError(f"output directory already exists; refusing stale output: {output_dir}")
    output_dir.parent.mkdir(parents=True, exist_ok=True)

    staging = Path(
        tempfile.mkdtemp(prefix=f".{output_dir.name}.staging-", dir=output_dir.parent)
    )
    cache: dict[str, tuple[TemplateDocument, tuple[str, ...], tuple[tuple[str, str], ...]]] = {}
    try:
        resolved_roots: list[tuple[str, tuple[str, ...], bytes]] = []
        for relative in roots:
            _, chain, values = resolve_template(input_root, relative, max_depth, cache=cache)
            resolved_roots.append((relative, chain, resolved_bytes(values)))

        input_records = [
            file_record(template_path(input_root, relative), relative)
            for relative in sorted(cache, key=lambda p: p.encode("utf-8"))
        ]
        total_input_bytes = sum(record["bytes"] for record in input_records)
        if total_input_bytes > MAX_TOTAL_BYTES:
            raise GeneratorError(
                f"templates exceed {MAX_TOTAL_BYTES} byte total safety bound: {total_input_bytes}"
            )

        output_records: list[dict[str, Any]] = []
        template_records: list[dict[str, Any]] = []
        for relative, chain, data in resolved_roots:
            destination = staging.joinpath(*PurePosixPath(relative).parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            output_records.append(
                {
                    "path": relative,
                    "source": relative,
                    "bytes": len(data),
                    "sha256": sha256_bytes(data),
                }
            )
            template_records.append(
                {"source": relative, "inheritance": list(chain)}
            )

        manifest: dict[str, Any] = {
            "schema": SCHEMA,
            "generator": {"name": GENERATOR_NAME, "version": generator_version},
            "inputs": input_records,
            "outputs": output_records,
            "templates": template_records,
            "manifest": {"path": manifest_name},
            "status": "pass",
            "errors": [],
        }
        (staging / manifest_name).write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
        os.chmod(staging / manifest_name, 0o644)
        os.replace(staging, output_dir)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return manifest


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-root", type=Path, required=True)
    parser.add_argument(
        "--template",
        dest="templates",
        action="append",
        required=True,
        help="explicit POSIX relative .ini template; repeat for each root template",
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--manifest-name", default="generated-manifest.json")
    parser.add_argument("--generator-version", default="1")
    parser.add_argument("--max-depth", type=int, default=DEFAULT_MAX_DEPTH)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        manifest = generate_templates(
            input_root=args.input_root,
            raw_templates=args.templates,
            output_dir=args.output_dir,
            manifest_name=args.manifest_name,
            generator_version=args.generator_version,
            max_depth=args.max_depth,
        )
    except (GeneratorError, OSError) as exc:
        print(f"Template manifest: fail: {exc}", file=sys.stderr)
        return 1
    print(
        f"Template manifest: pass ({args.output_dir / args.manifest_name}; "
        f"{len(manifest['outputs'])} outputs, {len(manifest['inputs'])} inputs)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
