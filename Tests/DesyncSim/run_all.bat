@echo off
rem run_all.bat — Run every test_*.exe in sequence and tally failures.
rem
rem Each executable's exit code is the number of failed assertions.
rem This script sums them all and reports a final total.
rem
rem Run this from the Tests\DesyncSim\ directory after building with build.bat.
rem
rem Exit code: 0 = all tests passed, nonzero = total failure count.

setlocal enabledelayedexpansion

set TOTAL_FAILURES=0
set TESTS_RUN=0
set TESTS_PASSED=0
set TESTS_FAILED=0

echo.
echo === Running DesyncSim tests ===
echo.

for %%T in (test_unordered_map_iteration.exe
            test_pathfind_memo_cooldown.exe
            test_thread_race_shape.exe
            test_fpu_mxcsr_determinism.exe
            test_uninit_struct_shape.exe
            test_lockstep_baseline.exe
            test_lockstep_injected_desync.exe) do (

    if exist %%T (
        echo --- %%T ---
        %%T
        set EXIT=!ERRORLEVEL!
        set /a TESTS_RUN+=1
        if !EXIT! equ 0 (
            echo [PASS] %%T
            set /a TESTS_PASSED+=1
        ) else (
            echo [FAIL] %%T  ^(!EXIT! assertion(s^) failed^)
            set /a TESTS_FAILED+=1
            set /a TOTAL_FAILURES+=!EXIT!
        )
        echo.
    ) else (
        echo [SKIP] %%T ^(not found — run build.bat first^)
        echo.
    )
)

echo ==============================
echo  Tests run:    %TESTS_RUN%
echo  Passed:       %TESTS_PASSED%
echo  Failed:       %TESTS_FAILED%
echo  Total assertions failed: %TOTAL_FAILURES%
echo ==============================
echo.

if %TOTAL_FAILURES% equ 0 (
    echo All tests PASSED.
) else (
    echo Some tests FAILED. See output above.
)

exit /b %TOTAL_FAILURES%
