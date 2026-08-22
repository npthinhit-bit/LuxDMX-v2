#!/usr/bin/env python3
"""Deterministic repository hygiene checks for LuxDMX-v2.

The checker distinguishes source-of-truth files from generated build output and
secret material. It is intentionally dependency-free so CI can run it before
installing PlatformIO or configuring a build directory.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Iterable


ALLOWED_SDKCONFIG_NAMES = {"sdkconfig.defaults"}
ALLOWED_SDKCONFIG_PREFIX = "sdkconfig.defaults."
GENERATED_DIRS = (
    ".pio",
    "test/build",
    "build",
    "cmake-build-debug",
    "cmake-build-release",
)
GENERATED_SUFFIXES = (".bin", ".elf", ".map")
SECRET_SUFFIXES = (".pem", ".p12", ".pfx", ".key")
SECRET_NAME_PARTS = (
    "private-key",
    "private_key",
    "privatekey",
    "signing-key",
    "signing_key",
    "signingkey",
    "id_rsa",
    "id_ed25519",
)


def normalize_paths(paths: Iterable[str]) -> list[str]:
    """Return sorted POSIX paths without duplicate separators or './'."""
    normalized = set()
    for raw_path in paths:
        path = str(raw_path).replace("\\", "/")
        while path.startswith("./"):
            path = path[2:]
        normalized.add(path)
    return sorted(normalized)


def _is_under(path: str, directory: str) -> bool:
    return path == directory or path.startswith(directory + "/")


def _is_allowed_sdkconfig(path: str) -> bool:
    name = PurePosixPath(path).name
    return name in ALLOWED_SDKCONFIG_NAMES or name.startswith(ALLOWED_SDKCONFIG_PREFIX)


def find_tracked_violations(paths: Iterable[str]) -> list[str]:
    """Find paths that must never be committed to the repository."""
    violations: list[str] = []
    for path in normalize_paths(paths):
        name = PurePosixPath(path).name.lower()
        lower_path = path.lower()
        reason = None
        if any(_is_under(lower_path, directory) for directory in GENERATED_DIRS):
            reason = "generated build directory"
        elif lower_path.startswith("sdkconfig.") and not _is_allowed_sdkconfig(path):
            reason = "generated sdkconfig; use tracked sdkconfig.defaults*"
        elif lower_path.endswith(GENERATED_SUFFIXES):
            reason = "generated firmware/build artifact"
        elif lower_path.endswith(SECRET_SUFFIXES):
            reason = "private-key or secret material"
        elif any(part in name for part in SECRET_NAME_PARTS):
            reason = "private-key or secret material"
        if reason:
            violations.append(f"{path}: {reason}")
    return violations


def find_worktree_violations(repo_root: Path) -> list[str]:
    """Find generated outputs already present in a checkout workspace."""
    violations: list[str] = []
    for directory in GENERATED_DIRS:
        candidate = repo_root / directory
        if candidate.exists():
            violations.append(f"{directory}: generated build directory exists")

    for candidate in repo_root.rglob("*"):
        if not candidate.is_file():
            continue
        relative = candidate.relative_to(repo_root).as_posix()
        lower = relative.lower()
        if lower.endswith(GENERATED_SUFFIXES):
            violations.append(f"{relative}: generated firmware/build artifact exists")
        elif lower.startswith("sdkconfig.") and not _is_allowed_sdkconfig(relative):
            violations.append(f"{relative}: generated sdkconfig exists")
    return sorted(set(violations))


def get_tracked_files(repo_root: Path) -> list[str]:
    result = subprocess.run(
        ["git", "-C", str(repo_root), "ls-files", "-z"],
        check=True,
        stdout=subprocess.PIPE,
    )
    return [item for item in result.stdout.decode("utf-8").split("\0") if item]


def check_git_diff(repo_root: Path) -> list[str]:
    result = subprocess.run(
        ["git", "-C", str(repo_root), "diff", "--check"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if result.returncode == 0:
        return []
    output = result.stdout.strip() or "git diff --check failed"
    return [f"whitespace: {line}" for line in output.splitlines()]


def check_repository(repo_root: Path, include_worktree: bool = True) -> list[str]:
    violations = find_tracked_violations(get_tracked_files(repo_root))
    if include_worktree:
        violations.extend(find_worktree_violations(repo_root))
    violations.extend(check_git_diff(repo_root))
    return sorted(set(violations))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="repository root (default: project root)",
    )
    parser.add_argument(
        "--tracked-only",
        action="store_true",
        help="skip workspace output checks; useful after local builds",
    )
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    if not (repo_root / ".git").exists():
        print(f"ERROR: not a repository root: {repo_root}", file=sys.stderr)
        return 2

    violations = check_repository(repo_root, include_worktree=not args.tracked_only)
    if violations:
        print("Repository hygiene: FAIL")
        for violation in violations:
            print(f"  - {violation}")
        return 1

    mode = "tracked files only" if args.tracked_only else "tracked files and workspace"
    print(f"Repository hygiene: PASS ({mode})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
