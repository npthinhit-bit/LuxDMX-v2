@echo off
REM Run all native host tests for LuxDMX-v2.
REM The script resolves its own location so it can be run from anywhere.
setlocal enabledelayedexpansion
set "V2_ROOT=%~dp0..\.."
cd /d "%V2_ROOT%"
set "FAILURES=0"
for %%T in (config_test seqlock_test merge_test rdm_types_test) do (
    echo.
    echo === %%T ===
    python build\test_native.py %%T
    if errorlevel 1 (
        set /a FAILURES+=1
        echo *** %%T FAILED ***
    )
)
echo.
echo !FAILURES! test suite(s) failed.
if !FAILURES!==0 (
    echo ALL TESTS PASSED
    exit /b 0
) else (
    exit /b 1
)
