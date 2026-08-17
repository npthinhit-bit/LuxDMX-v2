#!/usr/bin/env python3
"""Generate a manifest.json for a firmware build artifact.

Captures build metadata: environment, commit SHA, branch, build timestamp,
firmware size, SHA-256 hash, flash/RAM usage, and signing status.

Usage:
    python3 scripts/gen_manifest.py <env_name> <firmware_bin> [--elf <elf_path>] [--map <map_path>]
                                  [--sha256] [--signed <signed_bin>] [--output manifest.json]
                                  [--memory-report memory_report.json] [--commit <sha>] [--branch <name>]
"""
import sys
import os
import json
import hashlib
import subprocess
import pathlib
import argparse
from datetime import datetime, timezone


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(8192), b''):
            h.update(chunk)
    return h.hexdigest()


def get_git_info():
    info = {}
    try:
        info["commit"] = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], stderr=subprocess.DEVNULL
        ).decode().strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        info["commit"] = "unknown"

    try:
        info["short_commit"] = subprocess.check_output(
            ["git", "rev-parse", "--short=8", "HEAD"], stderr=subprocess.DEVNULL
        ).decode().strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        info["short_commit"] = info["commit"][:8] if len(info["commit"]) >= 8 else "unknown"

    try:
        info["branch"] = subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"], stderr=subprocess.DEVNULL
        ).decode().strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        info["branch"] = "unknown"

    try:
        info["dirty"] = bool(subprocess.check_output(
            ["git", "diff", "--quiet"], stderr=subprocess.DEVNULL
        ))
    except (subprocess.CalledProcessError, FileNotFoundError):
        info["dirty"] = False

    try:
        info["commit_timestamp"] = subprocess.check_output(
            ["git", "show", "-s", "--format=%cI", "HEAD"], stderr=subprocess.DEVNULL
        ).decode().strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        info["commit_timestamp"] = "unknown"

    return info


def main():
    parser = argparse.ArgumentParser(
        description="Generate a manifest.json for a firmware build"
    )
    parser.add_argument("env", help="PlatformIO environment name")
    parser.add_argument("firmware_bin", help="Path to firmware.bin")
    parser.add_argument("--elf", help="Path to firmware.elf")
    parser.add_argument("--map", help="Path to firmware.map")
    parser.add_argument("--sha256", action="store_true", help="Compute SHA-256 of firmware.bin")
    parser.add_argument("--signed", dest="signed_bin", help="Path to signed firmware.bin")
    parser.add_argument("--output", "-o", help="Output manifest.json path")
    parser.add_argument("--memory-report", help="Path to extract_sizes.py JSON output for memory usage")
    parser.add_argument("--commit", help="Override commit SHA")
    parser.add_argument("--branch", help="Override branch name")
    args = parser.parse_args()

    if not os.path.isfile(args.firmware_bin):
        print(f"ERROR: firmware binary not found: {args.firmware_bin}", file=sys.stderr)
        sys.exit(1)

    git_info = get_git_info()
    if args.commit:
        git_info["commit"] = args.commit
    if args.branch:
        git_info["branch"] = args.branch

    manifest = {
        "env": args.env,
        "build": {
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "commit": git_info["commit"],
            "short_commit": git_info["short_commit"],
            "branch": git_info["branch"],
            "dirty": git_info["dirty"],
            "commit_timestamp": git_info["commit_timestamp"],
        },
        "artifacts": {
            "firmware_bin": {
                "path": args.firmware_bin,
                "size_bytes": os.path.getsize(args.firmware_bin),
            }
        },
    }

    if args.sha256:
        manifest["artifacts"]["firmware_bin"]["sha256"] = sha256_file(args.firmware_bin)

    if args.elf and os.path.isfile(args.elf):
        manifest["artifacts"]["firmware_elf"] = {
            "path": args.elf,
            "size_bytes": os.path.getsize(args.elf),
            "sha256": sha256_file(args.elf),
        }

    if args.map and os.path.isfile(args.map):
        manifest["artifacts"]["firmware_map"] = {
            "path": args.map,
            "size_bytes": os.path.getsize(args.map),
            "sha256": sha256_file(args.map),
        }

    if args.signed_bin and os.path.isfile(args.signed_bin):
        sig_size = os.path.getsize(args.signed_bin) - os.path.getsize(args.firmware_bin)
        manifest["artifacts"]["firmware_signed"] = {
            "path": args.signed_bin,
            "size_bytes": os.path.getsize(args.signed_bin),
            "sha256": sha256_file(args.signed_bin),
            "signature_size_bytes": sig_size,
        }

    if args.memory_report and os.path.isfile(args.memory_report):
        with open(args.memory_report) as f:
            mem = json.load(f)
        manifest["memory"] = {
            "flash_bytes": mem.get("sections", {}).get("flash", 0),
            "ram_bytes": mem.get("sections", {}).get("ram", 0),
            "flash_pct": mem.get("usage_pct", {}).get("flash_pct", 0),
            "ram_pct": mem.get("usage_pct", {}).get("ram_pct", 0),
            "within_limits": mem.get("within_limits", False),
        }

    output = json.dumps(manifest, indent=2)
    print(output)

    if args.output:
        with open(args.output, 'w') as f:
            f.write(output)
        print(f"Manifest written to {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
