#!/usr/bin/env pwsh
# Local CI runner for LuxDMX-v2 (Windows PowerShell)
# Runs the same steps as GitHub Actions CI locally
# Usage: ./scripts/ci_local.ps1 [build|test|lint|all]

param(
    [ValidateSet('build', 'test', 'lint', 'all')]
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

function Write-Error($msg) {
    Write-Host "✗ $msg" -ForegroundColor $Red
}

function Check-PlatformIO {
    if (-not (Get-Command pio -ErrorAction SilentlyContinue)) {
        Write-Error "PlatformIO not found. Install with: pip install platformio"
        exit 1
    }
}

function Run-Build {
    $envs = @("esp32dev", "esp32s3dev", "wt32eth01", "esp32s3_psram", "esp32s3_n16r8_eth")
    $failed = 0

    Write-Step "Building all environments..."
    foreach ($env in $envs) {
        Write-Host "  Building $env..."
        try {
            pio run -e $env -v | Out-Null
            Write-Success "  $env built successfully"
        } catch {
            Write-Error "  $env build failed"
            $failed++
        }
    }

    if ($failed -gt 0) {
        Write-Error "$failed environment(s) failed to build"
        return $false
    }
    Write-Success "All environments built successfully"
    return $true
}

function Run-Tests {
    Write-Step "Running unit tests..."
    try {
        python build/test_native.py all
        Write-Success "All unit tests passed"
        return $true
    } catch {
        Write-Error "Unit tests failed"
        return $false
    }
}

function Run-Lint {
    Write-Step "Running lint (PlatformIO check)..."
    try {
        pio check --skip-packages --src-filters="+<src/> +<include/>" | Out-Null
        Write-Success "Lint passed"
        return $true
    } catch {
        Write-Error "Lint failed"
        return $false
    }
}

# Main
Check-PlatformIO

$success = $true

switch ($Command) {
    'build' { $success = Run-Build }
    'test'  { $success = Run-Tests }
    'lint'  { $success = Run-Lint }
    'all'   { $success = (Run-Build) -and (Run-Tests) -and (Run-Lint) }
}

if ($success) {
    Write-Success "All requested steps completed successfully"
    exit 0
} else {
    Write-Error "Some steps failed"
    exit 1
}