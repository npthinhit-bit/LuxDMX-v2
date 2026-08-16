#!/usr/bin/env pwsh
# Build all environments for LuxDMX-v2
# Usage: ./scripts/build_all.ps1

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectRoot = Split-Path -Parent $ScriptDir
Set-Location $ProjectRoot

$envs = @("esp32dev", "esp32s3dev", "wt32eth01", "esp32s3_psram", "esp32s3_n16r8_eth")

Write-Host "Building all environments..." -ForegroundColor Yellow

foreach ($env in $envs) {
    Write-Host "  Building $env..." -ForegroundColor Cyan
    try {
        pio run -e $env
        Write-Host "  ✓ $env built successfully" -ForegroundColor Green
    } catch {
        Write-Host "  ✗ $env build failed" -ForegroundColor Red
        exit 1
    }
}

Write-Host "All environments built successfully!" -ForegroundColor Green