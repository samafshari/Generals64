@echo off
rem build.bat — Compile all DesyncSim tests with MSVC.
rem
rem Run this from the Tests\DesyncSim\ directory after calling vcvarsall.bat
rem (or opening a Developer Command Prompt).
rem
rem Each test_*.exe is compiled next to this script.
rem Exit code = 0 if all compilations succeed, nonzero otherwise.
rem
rem Usage:
rem   cd Tests\DesyncSim
rem   build.bat

setlocal enabledelayedexpansion

set ERRORS=0
set CL_FLAGS=/std:c++20 /EHsc /nologo /W3

rem Shared infrastructure sources (compiled into every Tier-2 test).
set INFRA=FakeNetwork.cpp CrcCollector.cpp StubEngine.cpp GameInstance.cpp LockstepHarness.cpp

echo.
echo === Building DesyncSim tests ===
echo.

rem ---------------------------------------------------------------------------
rem Tier-1 micro-tests (single-file, no infra deps)
rem ---------------------------------------------------------------------------

echo [1/3] test_unordered_map_iteration
cl %CL_FLAGS% test_unordered_map_iteration.cpp /Fe:test_unordered_map_iteration.exe
if %ERRORLEVEL% neq 0 (
    echo   FAILED: test_unordered_map_iteration
    set /a ERRORS+=1
) else (
    echo   ok
)

echo [2/3] test_pathfind_memo_cooldown
cl %CL_FLAGS% test_pathfind_memo_cooldown.cpp /Fe:test_pathfind_memo_cooldown.exe
if %ERRORLEVEL% neq 0 (
    echo   FAILED: test_pathfind_memo_cooldown
    set /a ERRORS+=1
) else (
    echo   ok
)

echo [3/3] test_thread_race_shape
cl %CL_FLAGS% test_thread_race_shape.cpp /Fe:test_thread_race_shape.exe
if %ERRORLEVEL% neq 0 (
    echo   FAILED: test_thread_race_shape
    set /a ERRORS+=1
) else (
    echo   ok
)

rem ---------------------------------------------------------------------------
rem Tier-2 harness tests (require shared infrastructure)
rem ---------------------------------------------------------------------------

echo [4/5] test_lockstep_baseline
cl %CL_FLAGS% test_lockstep_baseline.cpp %INFRA% /Fe:test_lockstep_baseline.exe
if %ERRORLEVEL% neq 0 (
    echo   FAILED: test_lockstep_baseline
    set /a ERRORS+=1
) else (
    echo   ok
)

echo [5/5] test_lockstep_injected_desync
cl %CL_FLAGS% test_lockstep_injected_desync.cpp %INFRA% /Fe:test_lockstep_injected_desync.exe
if %ERRORLEVEL% neq 0 (
    echo   FAILED: test_lockstep_injected_desync
    set /a ERRORS+=1
) else (
    echo   ok
)

echo.
if %ERRORS% equ 0 (
    echo All compilations succeeded.
) else (
    echo %ERRORS% compilation(s) failed.
)

rem Clean up MSVC intermediate object files — keep only the .exe files.
del /q *.obj 2>nul

exit /b %ERRORS%
