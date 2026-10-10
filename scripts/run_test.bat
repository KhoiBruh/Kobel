@echo off
setlocal enabledelayedexpansion

if "%~1"=="" (
    echo [ERROR] Usage: run_test.bat ^<source_file.kb^>
    exit /b 1
)

set "SOURCE_FILE=%~1"
set "OUT_EXE=build\%~n1.exe"
if not exist "build" mkdir "build"

:: Setup MSVC if cl is not available in PATH
where cl >nul 2>nul
if %errorlevel% neq 0 (
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    )
)

:: The seed compiler is built from dist\bootstrap.c; build it on demand.
set "SEED=build\seed\kobel_seed.exe"
if not exist "%SEED%" (
    echo [INFO] Seed compiler not found, building it first...
    call "%~dp0build_seed.bat"
    if %errorlevel% neq 0 exit /b %errorlevel%
)

:: Compile
echo [BUILDING] Compiling %SOURCE_FILE% ...
"%SEED%" "%SOURCE_FILE%" -o "%OUT_EXE%"
if %errorlevel% neq 0 (
    echo [BUILD FAILED] Compilation aborted with error code %errorlevel%
    exit /b %errorlevel%
)

:: Run
echo [RUNNING] Executing %OUT_EXE% ...
%OUT_EXE%
set "RUN_CODE=%errorlevel%"

:: Cleanup executable and temporary files
if exist "%OUT_EXE%" del "%OUT_EXE%" >nul 2>nul
if exist "%OUT_EXE%.tmp.c" del "%OUT_EXE%.tmp.c" >nul 2>nul
if exist "%OUT_EXE%.tmp.obj" del "%OUT_EXE%.tmp.obj" >nul 2>nul
if exist "build\%~n1.obj" del "build\%~n1.obj" >nul 2>nul
if exist "build\%~n1.tmp.obj" del "build\%~n1.tmp.obj" >nul 2>nul
if exist "%~dpn1.tmp.obj" del "%~dpn1.tmp.obj" >nul 2>nul
if exist "%~dpn1.tmp.c" del "%~dpn1.tmp.c" >nul 2>nul

if %RUN_CODE% neq 0 (
    echo [TEST FAILED] Test exited with code %RUN_CODE%
    exit /b %RUN_CODE%
)

exit /b 0
