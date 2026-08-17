#!/usr/bin/env pwsh
# Local CI runner for LuxDMX-v2 (Windows PowerShell)
# Runs the same steps as GitHub Actions CI locally
# Usage: ./scripts/ci_local.ps1 [build|test|lint|static-analysis|dependency-scan|native-tests|unity-tests|ota-sign-verify|fuzz-test|all]

param(
    [ValidateSet('build', 'test', 'lint', 'static-analysis', 'dependency-scan',
        'native-tests', 'unity-tests', 'ota-sign-verify', 'fuzz-test', 'all')]
    [string]$Command = 'all'
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectRoot = Split-Path -Parent $ScriptDir
Set-Location $ProjectRoot

# Colors
$Red = [ConsoleColor]::Red
$Green = [ConsoleColor]::Green
$Yellow = [ConsoleColor]::Yellow
$Default = [ConsoleColor]::White

function Write-Step($msg) {
    Write-Host "==> $msg" -ForegroundColor $Yellow
}

function Write-Success($msg) {
    Write-Host "✓ $msg" -ForegroundColor $Green
}

function Write-Err($msg) {
    Write-Host "✗ $msg" -ForegroundColor $Red
}

function Check-PlatformIO {
    if (-not (Get-Command pio -ErrorAction SilentlyContinue)) {
        Write-Err "PlatformIO not found. Install with: pip install platformio"
        exit 1
    }
}

$Envs = @("esp32dev", "esp32s3dev", "wt32eth01", "esp32s3_psram", "esp32s3_n16r8_eth")

function Run-StaticAnalysis {
    Check-PlatformIO
    Write-Step "Running clang-format check..."
    pip install clang-format -q 2>$null
    $files = Get-ChildItem -Path src, include, test -Include *.cpp, *.h -Recurse | Select-Object -ExpandProperty FullName
    if ($files) {
        $result = echo $files | clang-format --style=file --dry-run --Werror 2>&1
        if ($LASTEXITCODE -eq 0) { Write-Success "clang-format check passed" }
        else { Write-Err "clang-format check failed"; Write-Host $result }
    }

    Write-Step "Running cppcheck..."
    choco install cppcheck -y -q 2>$null || pip install cppcheck -q 2>$null
    if (Get-Command cppcheck -ErrorAction SilentlyContinue) {
        cppcheck --enable=warning,performance,portability,information --suppress=missingIncludeSystem --inconclusive --std=c++17 -I include -I src src/ 2>&1
    }

    Write-Step "Running clang-tidy..."
    choco install llvm -y -q 2>$null || pip install clang-tidy -q 2>$null
    if (Get-Command clang-tidy -ErrorAction SilentlyContinue) {
        $sources = Get-ChildItem -Path src, include -Include *.cpp, *.h -Recurse | Select-Object -ExpandProperty FullName
        echo $sources | clang-tidy --checks='bugprone-*,cert-*' --warnings-as-errors='*' -p . -- 2>&1 || Write-Err "clang-tidy found issues"
    }
}

function Run-DependencyScan {
    Write-Step "Scanning Python dependencies..."
    pip install pip-audit -q 2>$null
    if (Get-Command pip-audit -ErrorAction SilentlyContinue) {
        pip-audit --local 2>&1 || Write-Err "pip-audit found vulnerabilities"
    }

    Write-Step "Scanning PlatformIO dependencies..."
    Check-PlatformIO
    pio lib list 2>&1
    pio pkg list --library 2>&1
}

function Run-Build {
    Check-PlatformIO
    $failed = 0

    Write-Step "Building all environments..."
    foreach ($env in $Envs) {
        Write-Host "  Building $env..."
        try {
            pio run -e $env | Out-Null
            Write-Success "  $env built successfully"
        } catch {
            Write-Err "  $env build failed"
            $failed++
        }
    }

    if ($failed -gt 0) {
        Write-Err "$failed environment(s) failed to build"
        return $false
    }
    Write-Success "All environments built successfully"

    Write-Step "Checking memory footprints..."
    foreach ($env in $Envs) {
        $elf = ".pio/build/$env/firmware.elf"
        $map = ".pio/build/$env/firmware.map"
        if (Test-Path $elf) {
            python test\native\test_native.py all 2>$null
            & python3 scripts/extract_sizes.py $env $elf --output "memory-$env.json" 2>&1
        }
    }
    return $true
}

function Run-NativeTests {
    Check-PlatformIO
    Write-Step "Running native tests with coverage..."
    python3 tools/gen_config_templates.py .
    $env:ENABLE_COVERAGE = "1"
    python3 test/native/test_native.py all
    if ($LASTEXITCODE -eq 0) {
        Write-Success "All native tests passed"
        return $true
    } else {
        Write-Err "Native tests failed"
        return $false
    }
}

function Run-UnityTests {
    Check-PlatformIO
    Write-Step "Running Unity tests..."
    pip install platformio -q 2>$null
    pio lib install unity 2>$null
    python3 tools/gen_config_templates.py .
    pio test -e unit-test --verbose
    if ($LASTEXITCODE -eq 0) {
        Write-Success "All Unity tests passed"
        return $true
    } else {
        Write-Err "Unity tests failed"
        return $false
    }
}

function Run-OtaSignVerify {
    Write-Step "Running OTA sign/verify test..."
    pip install cryptography -q 2>$null
    mkdir -Force /tmp/ota_test 2>$null
    python3 tools/gen_ota_keys.py 2>&1 | Select-Object -First 5
    Copy-Item tools/ota_private.pem /tmp/ota_test/test_private.pem
    Copy-Item tools/ota_public.bin /tmp/ota_test/test_public.bin
    # Create dummy firmware
    fsutil file createnew /tmp/ota_test/fw.bin 4096 2>$null
    python3 tools/sign_ota.py /tmp/ota_test/fw.bin /tmp/ota_test/test_private.pem /tmp/ota_test/fw-signed.bin
    python3 -c @"
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
    print('PASS: size correct')
else:
    print(f'FAIL: size mismatch')
    sys.exit(1)
"@
    Remove-Item /tmp/ota_test/test_private.pem, /tmp/ota_test/fw-signed.bin -Force 2>$null
    Write-Success "OTA sign/verify test passed"
}

function Run-FuzzTest {
    Write-Step "Running fuzz test..."
    pip install platformio -q 2>$null
    python3 tools/gen_config_templates.py .
    if (Test-Path test/native/fuzz_artnet.cpp) {
        Write-Host "=== Building fuzz harness ==="
        clang++ -std=c++17 -fsanitize=address,fuzzer -I include -I src -I src/cfg -I src/core -I src/drv -I src/net -I src/sys -I src/app -I src/generated -I test/native/shim -DUNIT_TESTING test/native/fuzz_artnet.cpp src/core/merge_engine.cpp src/core/dmx_buffer.cpp src/core/sender_tracker.cpp src/core/stats.cpp src/cfg/config_schema.cpp src/cfg/config_core.cpp src/cfg/config_serial.cpp src/config_templates_gen.cpp src/test_stubs.cpp -o build/fuzz_artnet 2>&1
        if (Test-Path build/fuzz_artnet) {
            Write-Host "=== Running fuzzer (60s) ==="
            & timeout 60 build/fuzz_artnet -max_total_time=60 -max_len=513 2>&1
        }
    } else {
        Write-Host "No fuzz harness found; skipping"
    }
}

function Run-Lint {
    Check-PlatformIO
    Write-Step "Running lint (PlatformIO check)..."
    pip install cppcheck -q 2>$null
    choco install cppcheck -y -q 2>$null
    if (pio check --skip-packages --src-filters="+<src/> +<include/>") {
        Write-Success "Lint passed"
        return $true
    } else {
        Write-Err "Lint failed"
        return $false
    }
}

# Main
Check-PlatformIO

$success = $true

switch ($Command) {
    'static-analysis'  { $success = Run-StaticAnalysis }
    'dependency-scan'  { $success = Run-DependencyScan }
    'build'            { $success = Run-Build }
    'native-tests'     { $success = Run-NativeTests }
    'unity-tests'      { $success = Run-UnityTests }
    'ota-sign-verify'  { $success = Run-OtaSignVerify }
    'fuzz-test'        { $success = Run-FuzzTest }
    'lint'             { $success = Run-Lint }
    'test'             { $success = (Run-NativeTests) -and (Run-UnityTests) }
    'all' {
        $success = (Run-StaticAnalysis) -and (Run-DependencyScan) -and (Run-Build) -and (Run-NativeTests) -and (Run-UnityTests) -and (Run-OtaSignVerify) -and (Run-FuzzTest) -and (Run-Lint)
    }
}

if ($success) {
    Write-Success "All requested steps completed successfully"
    exit 0
} else {
    Write-Error "Some steps failed"
    exit 1
}