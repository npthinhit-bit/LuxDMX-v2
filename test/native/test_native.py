#!/usr/bin/env python3
import os, sys, subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, "..", ".."))

INCLUDE_PATHS = ["-Iinclude", "-Isrc", "-Isrc/cfg", "-Isrc/core", "-Isrc/drv", "-Isrc/net", "-Isrc/sys", "-Isrc/app", "-Isrc/generated", "-Itest/native/shim"]

TEST_DEPS = {
    "config_test": ["src/cfg/config_core.cpp", "src/cfg/config_schema.cpp", "src/cfg/config_serial.cpp", "src/config_templates_gen.cpp", "src/test_stubs.cpp"],
    "seqlock_test": [],
    "merge_test": ["src/core/merge_engine.cpp", "src/core/dmx_buffer.cpp", "src/core/sender_tracker.cpp", "src/core/stats.cpp", "src/cfg/config_schema.cpp", "src/cfg/config_core.cpp", "src/cfg/config_serial.cpp", "src/config_templates_gen.cpp", "src/test_stubs.cpp"],
    "rdm_types_test": [],
}

COMMON_FLAGS = ["-std=c++17", "-DUNIT_TESTING"]


def build_cmd(test_name):
    if test_name not in TEST_DEPS:
        print("Unknown test: " + test_name, file=sys.stderr)
        print("Available: " + ", ".join(sorted(TEST_DEPS)), file=sys.stderr)
        sys.exit(2)
    test_cpp = os.path.join("test", "native", test_name + ".cpp")
    sources = [test_cpp] + TEST_DEPS[test_name]
    build_dir = os.path.join("build", "test_native")
    os.makedirs(build_dir, exist_ok=True)
    exe_path = os.path.join(build_dir, test_name)
    cmd = ["g++"] + COMMON_FLAGS + INCLUDE_PATHS + sources + ["-o", exe_path]
    return cmd, exe_path


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 test/native/test_native.py <test_name>", file=sys.stderr)
        sys.exit(2)
    test_name = sys.argv[1]
    cmd, exe_path = build_cmd(test_name)
    print("=== Compiling " + test_name + " ===")
    print(" ".join(cmd))
    result = subprocess.run(cmd, cwd=PROJECT_ROOT)
    if result.returncode != 0:
        print("COMPILATION FAILED for " + test_name)
        sys.exit(1)
    print("=== Running " + test_name + " ===")
    result = subprocess.run([exe_path], cwd=PROJECT_ROOT)
    if result.returncode == 0:
        print(test_name + ": PASS")
    else:
        print(test_name + ": FAIL")
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
