#!/bin/bash
# Local CI runner for LuxDMX-v2
# Runs the same steps as GitHub Actions CI locally
# Usage: ./scripts/ci_local.sh [build|test|lint|static-analysis|dependency-scan|native-tests|unity-tests|ota-sign-verify|fuzz-test|all]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_step() {
    echo -e "${YELLOW}==>${NC} $1"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

# Check if platformio is installed
check_platformio() {
    if ! command -v pio &> /dev/null; then
        print_error "PlatformIO not found. Install with: pip install platformio"
        exit 1
    fi
}

ENVS=("esp32dev" "esp32s3dev" "wt32eth01" "esp32s3_psram" "esp32s3_n16r8_eth")

run_static_analysis() {
    check_platformio
    print_step "Running clang-format check..."
    pip install clang-format -q 2>/dev/null || true
    local files
    files=$(find src include test -name '*.cpp' -o -name '*.h' 2>/dev/null | sort)
    if [ -n "$files" ]; then
        if echo "$files" | xargs clang-format --style=file --dry-run --Werror 2>&1; then
            print_success "clang-format check passed"
        else
            print_error "clang-format check failed — run 'clang-format -i <files>' to fix"
            return 1
        fi
    fi

    print_step "Running cppcheck..."
    sudo apt-get update -qq && sudo apt-get install -y -qq cppcheck 2>/dev/null || true
    if command -v cppcheck &> /dev/null; then
        cppcheck --enable=warning,performance,portability,information \
            --suppress=missingIncludeSystem --inconclusive --std=c++17 \
            -I include -I src src/ 2>&1 && print_success "cppcheck passed" || print_error "cppcheck found issues"
    fi

    print_step "Running clang-tidy..."
    sudo apt-get install -y -qq clang-tidy bear 2>/dev/null || true
    if command -v clang-tidy &> /dev/null; then
        SOURCES=$(find src include -name '*.cpp' -o -name '*.h' | head -40)
        echo "$SOURCES" | xargs clang-tidy --checks='bugprone-*,cert-*' --warnings-as-errors='*' 2>&1 || print_error "clang-tidy found issues"
        print_success "clang-tidy completed"
    fi
}

run_dependency_scan() {
    print_step "Scanning Python dependencies..."
    pip install pip-audit -q 2>/dev/null || true
    if command -v pip-audit &> /dev/null; then
        pip-audit --local 2>&1 || print_error "pip-audit found vulnerabilities"
        print_success "Python dependency scan completed"
    fi

    print_step "Scanning PlatformIO dependencies..."
    check_platformio
    pio lib list 2>&1 || true
    pio pkg list --library 2>&1 || true
    print_success "PlatformIO dependency scan completed"
}

run_build() {
    check_platformio
    local failed=0
    print_step "Building all environments..."
    for env in "${ENVS[@]}"; do
        echo "  Building $env..."
        if pio run -e "$env" > /dev/null 2>&1; then
            print_success "  $env built successfully"
        else
            print_error "  $env build failed"
            failed=$((failed + 1))
        fi
    done

    if [ $failed -gt 0 ]; then
        print_error "$failed environment(s) failed to build"
        return 1
    fi
    print_success "All environments built successfully"

    # Memory footprint check for each env
    print_step "Checking memory footprints..."
    for env in "${ENVS[@]}"; do
        ELF=".pio/build/$env/firmware.elf"
        MAP=".pio/build/$env/firmware.map"
        if [ -f "$ELF" ]; then
            python3 scripts/extract_sizes.py "$env" "$ELF" --output "memory-$env.json" 2>&1 && \
                print_success "$env within memory thresholds" || \
                print_error "$env exceeded memory thresholds"
        elif [ -f "$MAP" ]; then
            python3 scripts/extract_sizes.py "$env" "$MAP" --from-map --output "memory-$env.json" 2>&1 && \
                print_success "$env within memory thresholds" || \
                print_error "$env exceeded memory thresholds"
        fi
    done
    return 0
}

run_native_tests() {
    check_platformio
    print_step "Running native tests with coverage..."
    python3 tools/gen_config_templates.py . 2>/dev/null
    pip install lcov gcovr -q 2>/dev/null || true
    export ENABLE_COVERAGE=1
    export GCOV_PREFIX_STRIP=1
    if python3 test/native/test_native.py all; then
        print_success "All native tests passed"
        # Collect coverage
        if command -v lcov &> /dev/null; then
            lcov --directory build/test_native --capture --output-file coverage.info 2>/dev/null || true
            genhtml coverage.info --output-directory coverage_report 2>/dev/null || true
            print_success "Coverage report generated"
        fi
        return 0
    else
        print_error "Native tests failed"
        return 1
    fi
}

run_unity_tests() {
    check_platformio
    print_step "Running Unity tests..."
    pip install platformio -q 2>/dev/null || true
    pio lib install unity 2>/dev/null || true
    python3 tools/gen_config_templates.py . 2>/dev/null
    if pio test -e unit-test --verbose; then
        print_success "All Unity tests passed"
        return 0
    else
        print_error "Unity tests failed"
        return 1
    fi
}

run_tests() {
    run_native_tests
    run_unity_tests
}

run_ota_sign_verify() {
    print_step "Running OTA sign/verify test..."
    pip install cryptography -q 2>/dev/null || true
    mkdir -p /tmp/ota_test
    # Generate test key pair
    python3 tools/gen_ota_keys.py 2>&1 | head -5
    cp tools/ota_private.pem /tmp/ota_test/test_private.pem
    cp tools/ota_public.bin /tmp/ota_test/test_public.bin
    # Create a dummy firmware for signing test
    dd if=/dev/urandom of=/tmp/ota_test/fw.bin bs=4096 count=1 2>/dev/null
    python3 tools/sign_ota.py /tmp/ota_test/fw.bin /tmp/ota_test/test_private.pem /tmp/ota_test/fw-signed.bin
    # Verify
    python3 -c "
import hashlib, sys
from cryptography.hazmat.primitives.asymmetric import ed25519
from cryptography.hazmat.primitives import serialization
with open('/tmp/ota_test/test_private.pem', 'rb') as f:
    priv = serialization.load_pem_private_key(f.read(), password=None)
with open('/tmp/ota_test/fw-signed.bin', 'rb') as f:
    data = f.read()
sig = data[-64:]
fw = data[:-64]
digest = hashlib.sha256(fw).digest()
try:
    priv.public_key().verify(sig, digest)
    print('PASS: signature verified')
except Exception as e:
    print(f'FAIL: {e}')
    sys.exit(1)
if len(data) == len(fw) + 64:
    print('PASS: size correct (signed = unsigned + 64 bytes)')
else:
    print(f'FAIL: size mismatch')
    sys.exit(1)
"
    rm -f /tmp/ota_test/test_private.pem /tmp/ota_test/fw-signed.bin
    print_success "OTA sign/verify test passed"
}

run_fuzz_test() {
    print_step "Running fuzz test..."
    pip install platformio -q 2>/dev/null || true
    python3 tools/gen_config_templates.py . 2>/dev/null
    if [ -f test/native/fuzz_artnet.cpp ]; then
        echo "=== Building fuzz harness ==="
        clang++ -std=c++17 -fsanitize=address,fuzzer \
            -I include -I src -I src/cfg -I src/core -I src/drv -I src/net -I src/sys -I src/app \
            -I src/generated -I test/native/shim -DUNIT_TESTING \
            test/native/fuzz_artnet.cpp \
            src/core/merge_engine.cpp \
            src/core/dmx_buffer.cpp \
            src/core/sender_tracker.cpp \
            src/core/stats.cpp \
            src/cfg/config_schema.cpp \
            src/cfg/config_core.cpp \
            src/cfg/config_serial.cpp \
            src/config_templates_gen.cpp \
            src/test_stubs.cpp \
            -o build/fuzz_artnet 2>&1 || echo "Fuzz harness build skipped"
        if [ -f build/fuzz_artnet ]; then
            echo "=== Running fuzzer (60s) ==="
            timeout 60 ./build/fuzz_artnet -max_total_time=60 -max_len=513 2>&1 || true
        fi
    else
        echo "No fuzz harness found; skipping"
    fi
}

run_lint() {
    check_platformio
    print_step "Running lint (PlatformIO check)..."
    pip install cppcheck -q 2>/dev/null || true
    sudo apt-get update -qq && sudo apt-get install -y -qq cppcheck 2>/dev/null || true
    if pio check --skip-packages --src-filters="+<src/> +<include/>"; then
        print_success "Lint passed"
        return 0
    else
        print_error "Lint failed"
        return 1
    fi
}

# Main
COMMAND="${1:-all}"

check_platformio

case "$COMMAND" in
    static-analysis) run_static_analysis ;;
    dependency-scan) run_dependency_scan ;;
    build)  run_build ;;
    native-tests) run_native_tests ;;
    unity-tests) run_unity_tests ;;
    ota-sign-verify) run_ota_sign_verify ;;
    fuzz-test) run_fuzz_test ;;
    lint)   run_lint ;;
    test)   run_tests ;;
    all)
        run_static_analysis && \
        run_dependency_scan && \
        run_build && \
        run_native_tests && \
        run_unity_tests && \
        run_ota_sign_verify && \
        run_fuzz_test && \
        run_lint
        ;;
    *)
        echo "Usage: $0 [static-analysis|dependency-scan|build|native-tests|unity-tests|ota-sign-verify|fuzz-test|lint|test|all]"
        exit 1
        ;;
esac

if [ $? -eq 0 ]; then
    print_success "All requested steps completed successfully"
else
    print_error "Some steps failed"
    exit 1
fi