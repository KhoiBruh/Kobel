@echo off
setlocal enabledelayedexpansion

echo ============================================================
echo   Building Kobel seed compiler from dist\bootstrap.c
echo   Output: build\seed\kobel_seed.exe
echo ============================================================

:: Setup MSVC if cl is not available in PATH
where cl >nul 2>nul
if %errorlevel% neq 0 (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
    )
)

if not exist "build" mkdir "build"
if not exist "build\seed" mkdir "build\seed"

:: Pin cl's temporary directory: a shell whose TMP points at a deleted or read-only
:: folder makes the link step fail with D8018. Keep build debris inside build\.
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
