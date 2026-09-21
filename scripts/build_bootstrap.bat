@echo off
setlocal enabledelayedexpansion

echo ============================================================
echo   Building Kobel Bootstrap Compiler v1 (kobel_v1.exe)
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

echo [BUILDING] Compiling src\bootstrap\main.kb to kobel_v1.exe ...
.\cmake-build-debug\Kobel.exe "src\bootstrap\main.kb" -o "kobel_v1.exe"
if %errorlevel% neq 0 (
    echo [BUILD FAILED] Failed to compile bootstrap compiler.
    exit /b %errorlevel%
)

echo [SUCCESS] Successfully built kobel_v1.exe!
kobel_v1.exe --version
exit /b 0
