#!/usr/bin/env python3
"""Capture safe, deterministic CI diagnostics for LuxDMX-v2.

Only an allowlist of command output is written. The process environment is
never serialized, which prevents tokens and signing material from leaking into
failure artifacts.
"""
from __future__ import annotations

import argparse
import os
import platform
import subprocess
from pathlib import Path


COMMANDS: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("git-revision.txt", ("git", "rev-parse", "HEAD")),
    ("git-status.txt", ("git", "status", "--short")),
    ("python-version.txt", ("python3", "--version")),
    ("cmake-version.txt", ("cmake", "--version")),
    ("ctest-version.txt", ("ctest", "--version")),
)


def run_command(command: tuple[str, ...], cwd: Path) -> str:
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"UNAVAILABLE: {exc}\n"
    output = result.stdout or ""
    if result.returncode != 0:
        return f"EXIT {result.returncode}\n{output}"
    return output if output.endswith("\n") else output + "\n"


def capture_context(repo_root: Path, output_dir: Path, label: str) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "label.txt").write_text(label + "\n", encoding="utf-8")
    (output_dir / "platform.txt").write_text(
        f"system={platform.system()}\n"
        f"release={platform.release()}\n"
        f"machine={platform.machine()}\n"
        f"python={platform.python_version()}\n",
        encoding="utf-8",
    )
    for filename, command in COMMANDS:
        (output_dir / filename).write_text(
            run_command(command, repo_root), encoding="utf-8"
        )
    (output_dir / "environment-policy.txt").write_text(
        "Environment variables are intentionally not captured.\n"
        "Secrets must remain in the CI secret store and outside artifacts.\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--label", required=True)
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()
    if not (repo_root / ".git").exists():
        parser.error(f"not a repository root: {repo_root}")
    capture_context(repo_root, args.output_dir.resolve(), args.label)
    print(f"CI context captured: {args.output_dir.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
