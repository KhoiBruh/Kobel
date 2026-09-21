@echo off
:: TEMP diagnostic wrapper: sets up MSVC env then invokes the v0 compiler
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
"%~dp0..\cmake-build-debug\Kobel.exe" %*
