@echo off
setlocal enabledelayedexpansion

echo ============================================================
echo   Building Kobel seed compiler from dist\bootstrap.c
echo   Output: build\seed\kobel_seed.exe
echo ============================================================

if not exist "build" mkdir "build"
if not exist "build\seed" mkdir "build\seed"

:: Keep the compiler's scratch files inside the repository: some shells deny
:: %TEMP% to child processes, which breaks every C compiler.
set "TMP=%CD%\build\seed"
set "TEMP=%CD%\build\seed"

echo [BUILDING] Compiling dist\bootstrap.c to build\seed\kobel_seed.exe ...
cl /nologo /O2 /Fe:build\seed\kobel_seed.exe /Fo:build\seed\bootstrap.obj "dist\bootstrap.c"
if %errorlevel% neq 0 (
    echo [BUILD FAILED] Failed to compile the seed compiler.
    exit /b %errorlevel%
)

echo [SUCCESS] Seed compiler built at build\seed\kobel_seed.exe
exit /b 0
