@echo off
setlocal enabledelayedexpansion

if "%~1"=="" (
    echo [ERROR] Usage: run_test.bat ^<source_file.kb^>
    exit /b 1
)

set "SOURCE_FILE=%~1"
set "OUT_EXE=%~n1.exe"

:: Setup MSVC if cl is not available in PATH
where cl >nul 2>nul
if %errorlevel% neq 0 (
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    )
)

:: Compile
echo [BUILDING] Compiling %SOURCE_FILE% ...
.\cmake-build-debug\Kobel.exe "%SOURCE_FILE%" -o "%OUT_EXE%"
if %errorlevel% neq 0 (
    echo [BUILD FAILED] Compilation aborted with error code %errorlevel%
    exit /b %errorlevel%
)

:: Run
echo [RUNNING] Executing %OUT_EXE% ...
.\%OUT_EXE%
set "RUN_CODE=%errorlevel%"

:: Cleanup executable
if exist "%OUT_EXE%" del "%OUT_EXE%" >nul 2>nul
set "OBJ_FILE=%~n1.obj"
if exist "%OBJ_FILE%" del "%OBJ_FILE%" >nul 2>nul

if %RUN_CODE% neq 0 (
    echo [TEST FAILED] Test exited with code %RUN_CODE%
    exit /b %RUN_CODE%
)

exit /b 0
