#!/bin/bash
# Local CI runner for LuxDMX-v2
# Runs the same steps as GitHub Actions CI locally
# Usage: ./scripts/ci_local.sh [build|test|lint|all]

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

run_build() {
    local envs=("esp32dev" "esp32s3dev" "wt32eth01" "esp32s3_psram" "esp32s3_n16r8_eth")
    local failed=0

    print_step "Building all environments..."
    for env in "${envs[@]}"; do
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
    return 0
}

run_tests() {
    print_step "Running unit tests..."
    if python build/test_native.py all; then
        print_success "All unit tests passed"
        return 0
    else
        print_error "Unit tests failed"
        return 1
    fi
}

run_lint() {
    print_step "Running lint (PlatformIO check)..."
    if pio check --skip-packages --src-filters="+<src/> +<include/>" > /dev/null 2>&1; then
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
    build)
        run_build
        ;;
    test)
        run_tests
        ;;
    lint)
        run_lint
        ;;
    all)
        run_build && run_tests && run_lint
        ;;
    *)
        echo "Usage: $0 [build|test|lint|all]"
        exit 1
        ;;
esac

if [ $? -eq 0 ]; then
    print_success "All requested steps completed successfully"
else
    print_error "Some steps failed"
    exit 1
fi