#!/usr/bin/env python3
"""Create a stable, bounded metadata report for one PlatformIO firmware env."""
from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import os
import platform
import subprocess
import sys
from pathlib import Path
from typing import Any

CHUNK_BYTES = 1024 * 1024
MAX_INPUT_BYTES = 64 * 1024 * 1024
REQUIRED_ARTIFACTS = ("firmware.bin", "partitions.bin", "bootloader.bin")
OPTIONAL_ARTIFACTS = ("firmware.elf",)


def run_text(command: list[str], cwd: Path) -> str:
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=15,
        )
    except (OSError, subprocess.TimeoutExpired):
        return "unknown"
    if result.returncode != 0:
        return "unknown"
    return result.stdout.strip() or "unknown"


def resolve_platformio_section(
    parser: configparser.ConfigParser, section: str, seen: set[str] | None = None
) -> dict[str, str]:
    """Resolve PlatformIO `extends` values without interpolation or cycles."""
    if seen is None:
        seen = set()
    if section in seen or not parser.has_section(section):
        return {}
    seen.add(section)
    values: dict[str, str] = {}
    parent = parser.get(section, "extends", fallback="").strip()
    if parent:
        values.update(resolve_platformio_section(parser, parent, seen))
    values.update({key: value.strip() for key, value in parser.items(section)})
    return values


def sha256_file(path: Path) -> tuple[int, str]:
    size = path.stat().st_size
    if size > MAX_INPUT_BYTES:
        raise ValueError(f"{path.name} exceeds {MAX_INPUT_BYTES} byte safety bound")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(CHUNK_BYTES):
            digest.update(chunk)
    return size, digest.hexdigest()


def git_commit(repo_root: Path) -> str:
    return os.environ.get("GITHUB_SHA") or run_text(
        ["git", "rev-parse", "HEAD"], repo_root
    )


def build_report(
    repo_root: Path,
    environment: str,
    build_dir: Path,
    platformio_ini: Path,
) -> tuple[dict[str, Any], list[str]]:
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(platformio_ini, encoding="utf-8")
    values = resolve_platformio_section(parser, f"env:{environment}")
    errors: list[str] = []

    board = values.get("board", "unknown")
    profile = "release" if environment.endswith("_release") else "development"
    if not values:
        errors.append(f"environment not found in platformio.ini: {environment}")

    artifacts: dict[str, dict[str, Any]] = {}
    for name in (*REQUIRED_ARTIFACTS, *OPTIONAL_ARTIFACTS):
        path = build_dir / name
        item: dict[str, Any] = {
            "path": str(path.relative_to(repo_root))
            if path.is_relative_to(repo_root)
            else str(path),
            "present": path.is_file(),
        }
        if path.is_file():
            try:
                size, digest = sha256_file(path)
                item["bytes"] = size
                item["sha256"] = digest
            except (OSError, ValueError) as exc:
                errors.append(str(exc))
                item["error"] = str(exc)
        else:
            item["bytes"] = 0
            item["sha256"] = ""
            if name in REQUIRED_ARTIFACTS:
                errors.append(f"missing required artifact: {path}")
        artifacts[name] = item

    report: dict[str, Any] = {
        "schema": "luxdmx.firmware-artifact.v1",
        "status": "fail" if errors else "pass",
        "environment": environment,
        "board": board,
        "profile": profile,
        "commit": git_commit(repo_root),
        "platform": values.get("platform", "unknown"),
        "framework": values.get("framework", "unknown"),
        "platformio_version": run_text(["pio", "--version"], repo_root),
        "python_version": platform.python_version(),
        "artifacts": artifacts,
        "errors": errors,
    }
    return report, errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--environment", required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--platformio-ini", type=Path)
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    platformio_ini = (args.platformio_ini or repo_root / "platformio.ini").resolve()
    build_dir = args.build_dir.resolve()
    if not platformio_ini.is_file():
        parser.error(f"platformio.ini not found: {platformio_ini}")

    report, errors = build_report(repo_root, args.environment, build_dir, platformio_ini)
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(f"Firmware artifact report: {report['status']} ({output})")
    for error in errors:
        print(f"  - {error}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
