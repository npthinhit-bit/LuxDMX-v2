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

# Source files that pull in the generated config_templates.gen.h header.
# Tests whose deps intersect this set need the header generated first.
TEMPLATE_DEPS = {"src/config_templates_gen.cpp"}


def generateTemplates():
    """Generate src/generated/config_templates.gen.h from templates/*.ini.

    This header is a build-time artifact produced by tools/gen_config_templates.py
    (invoked by extra_scripts.py during PlatformIO builds). The standalone native
    test runner bypasses PlatformIO, so it must generate the header itself or
    config_test/merge_test fail with a fatal 'No such file or directory' error
    on the missing header.
    """
    gen_script = os.path.join(PROJECT_ROOT, "tools", "gen_config_templates.py")
    if not os.path.isfile(gen_script):
        print("WARNING: gen_config_templates.py not found; skipping template generation",
              file=sys.stderr)
        return
    result = subprocess.run(
        [sys.executable, gen_script, PROJECT_ROOT], cwd=PROJECT_ROOT
    )
    if result.returncode != 0:
        print("ERROR: failed to generate config_templates.gen.h", file=sys.stderr)
        sys.exit(1)


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
    # Generate config_templates.gen.h when this test compiles sources that
    # depend on it. PlatformIO normally does this via extra_scripts.py; the
    # standalone native runner must do it itself.
    if TEMPLATE_DEPS & set(TEST_DEPS[test_name]):
        generateTemplates()
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
