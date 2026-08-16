#!/bin/bash
# Build all environments for LuxDMX-v2
# Usage: ./scripts/build_all.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

ENVS=("esp32dev" "esp32s3dev" "wt32eth01" "esp32s3_psram" "esp32s3_n16r8_eth")

echo -e "\033[1;33mBuilding all environments...\033[0m"

for env in "${ENVS[@]}"; do
    echo -e "  \033[1;36mBuilding $env...\033[0m"
    if pio run -e "$env"; then
        echo -e "  \033[1;32m✓ $env built successfully\033[0m"
    else
        echo -e "  \033[1;31m✗ $env build failed\033[0m"
        exit 1
    fi
done

echo -e "\n\033[1;32mAll environments built successfully!\033[0m"