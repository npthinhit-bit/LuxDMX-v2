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

# Coverage flags — only enabled when ENABLE_COVERAGE env var is set (CI only).
# MSVC does not support --coverage, so this is g++/clang-only.
ENABLE_COVERAGE = os.environ.get("ENABLE_COVERAGE", "").lower() in ("1", "true", "yes")
COVERAGE_FLAGS = ["--coverage", "-fprofile-arcs", "-ftest-coverage"] if ENABLE_COVERAGE else []

COMMON_FLAGS = ["-std=c++17", "-DUNIT_TESTING"] + COVERAGE_FLAGS

# Source files that pull in the generated config_templates.gen.h header.
# Tests whose deps intersect this set need the header generated first.
TEMPLATE_DEPS = {"src/config_templates_gen.cpp"}


def generateTemplates():
    """Generate src/generated/config_templates.gen.h from templates/*.ini.

    This header is a build-time artifact produced by tools/gen_config_templates.py
    (invoked by extra_scripts.py during PlatformIO builds). The standalone native
    test runner bypasses PlatformIO's build hooks, so it must generate the header
    itself or config_test/merge_test fail with a fatal 'No such file or directory'
    error on the missing header.
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
    # Direct coverage output to the build dir so lcov/gcovr can find it
    if ENABLE_COVERAGE:
        cmd.extend(["-fprofile-dir=build/test_native/coverage"])
    return cmd, exe_path


def run_single_test(test_name):
    """Compile and run a single test suite. Returns True on PASS, False on FAIL."""
    # Clean up stale .gcda files from previous runs to avoid stale coverage data
    if ENABLE_COVERAGE:
        import glob
        for gcda in glob.glob(os.path.join(PROJECT_ROOT, "build", "test_native", "*.gcda")):
            os.remove(gcda)
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
        return False
    print("=== Running " + test_name + " ===")
    result = subprocess.run([exe_path], cwd=PROJECT_ROOT)
    if result.returncode == 0:
        print(test_name + ": PASS")
        return True
    else:
        print(test_name + ": FAIL")
        return False


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 test/native/test_native.py <test_name|all>", file=sys.stderr)
        print("Available: " + ", ".join(sorted(TEST_DEPS)) + " or 'all'", file=sys.stderr)
        sys.exit(2)
    target = sys.argv[1]
    if target == "all":
        failures = 0
        for t in sorted(TEST_DEPS):
            ok = run_single_test(t)
            print()
            if not ok:
                failures += 1
        if failures:
            print(str(failures) + " test suite(s) FAILED")
            sys.exit(1)
        print("ALL TESTS PASSED")
        return
    if target not in TEST_DEPS:
        print("Unknown test: " + target, file=sys.stderr)
        print("Tests: " + ", ".join(sorted(TEST_DEPS)) + " or 'all'", file=sys.stderr)
        sys.exit(2)
    ok = run_single_test(target)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()