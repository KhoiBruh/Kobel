@echo off
setlocal enabledelayedexpansion

echo ============================================================
echo   Building Kobel seed compiler from dist\bootstrap.c
echo   Output: build\seed\kobel_seed.exe
echo ============================================================

:: Setup MSVC if cl is not available in PATH
where cl >nul 2>nul
if %errorlevel% neq 0 (
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    )
)

if not exist "build\seed" mkdir "build\seed"

echo [BUILDING] Compiling dist\bootstrap.c to build\seed\kobel_seed.exe ...
cl /nologo /O2 /Fe:build\seed\kobel_seed.exe /Fo:build\seed\bootstrap.obj "dist\bootstrap.c"
if %errorlevel% neq 0 (
    echo [BUILD FAILED] Failed to compile the seed compiler.
    exit /b %errorlevel%
)

echo [SUCCESS] Seed compiler built at build\seed\kobel_seed.exe
exit /b 0
