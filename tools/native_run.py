#!/usr/bin/env python3
"""Native test runner for LuxDMX-v2.

Wraps cmake configure + build + ctest into a single command so the native
ESP-IDF-shim host test harness can be driven from the repo root or CI.

Usage:
    python3 tools/native_run.py            # configure (if needed) + build + test
    python3 tools/native_run.py --clean      # wipe build dir, fresh build + test
    python3 tools/native_run.py --verbose  # show per-test output (-V to ctest)
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC_DIR_NAME = "test"
BUILD_DIR_NAME = "build"
CMAKE_GENERATOR = "Ninja"


def phase(msg: str) -> None:
    print(f"\n=== {msg} ===", flush=True)


def run(cmd: list[str]) -> int:
    print(f"$ {' '.join(cmd)}", flush=True)
    return subprocess.run(cmd).returncode


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run the LuxDMX-v2 native test harness (cmake + ctest).",
    )
    parser.add_argument(
        "--project-dir",
        default=str(REPO_ROOT),
        metavar="PATH",
        help="Project root containing the test/ directory (default: repo root).",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Wipe the build directory before configuring.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Show individual test output (pass --output-on-failure -V to ctest).",
    )
    args = parser.parse_args()

    project_dir = Path(args.project_dir).resolve()
    src_dir = project_dir / SRC_DIR_NAME
    build_dir = src_dir / BUILD_DIR_NAME
    cmakelists = src_dir / "CMakeLists.txt"

    if not cmakelists.is_file():
        print(
            f"ERROR: {cmakelists} not found. "
            "Is --project-dir pointing at the LuxDMX-v2 repo root?",
            file=sys.stderr,
        )
        return 2

    if args.clean:
        phase(f"clean: removing {build_dir}")
        if build_dir.exists():
            shutil.rmtree(build_dir)

    if not (build_dir / "CMakeCache.txt").is_file():
        phase("configure: cmake")
        rc = run(
            ["cmake", "-S", str(src_dir), "-B", str(build_dir), "-G", CMAKE_GENERATOR]
        )
        if rc != 0:
            print(f"ERROR: cmake configure failed (exit {rc})", file=sys.stderr)
            return rc
    else:
        print("configure: build directory present, skipping", flush=True)

    phase("build: cmake --build")
    rc = run(["cmake", "--build", str(build_dir)])
    if rc != 0:
        print(f"ERROR: build failed (exit {rc})", file=sys.stderr)
        return rc

    phase("test: ctest")
    ctest_cmd = ["ctest", "--test-dir", str(build_dir)]
    if args.verbose:
        ctest_cmd += ["--output-on-failure", "-V"]
    rc = run(ctest_cmd)
    if rc != 0:
        print(f"ERROR: tests failed (exit {rc})", file=sys.stderr)
        return rc

    phase("all done - tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
