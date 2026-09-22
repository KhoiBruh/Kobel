@echo off
setlocal enabledelayedexpansion

echo ============================================================
echo      Running Full Kobel Bootstrap Test Suite
echo ============================================================

set "FAILED_TESTS="
set "PASS_COUNT=0"
set "FAIL_COUNT=0"

set "TEST_FILES=examples\test_bootstrap_lexer.kb examples\test_bootstrap_ast.kb examples\test_bootstrap_parser.kb examples\test_bootstrap_sema_types.kb examples\test_bootstrap_sema_symbol.kb examples\test_bootstrap_sema_decl.kb examples\test_bootstrap_sema_body.kb examples\test_bootstrap_codegen.kb examples\test_bootstrap_for.kb examples\test_bootstrap_interp.kb examples\test_bootstrap_trait.kb examples\test_bootstrap_fmt.kb"

for %%F in (%TEST_FILES%) do (
    if exist "%%F" (
        echo.
        echo ------------------------------------------------------------
        echo [*] Running: %%F
        echo ------------------------------------------------------------
        call scripts\run_test.bat "%%F"
        if !errorlevel! equ 0 (
            set /a PASS_COUNT+=1
            echo [PASS] %%F
        ) else (
            set /a FAIL_COUNT+=1
            set "FAILED_TESTS=!FAILED_TESTS! %%F"
            echo [FAIL] %%F
        )
    )
)

echo.
echo ============================================================
echo                      TEST SUMMARY
echo ============================================================
echo Passed: %PASS_COUNT%
echo Failed: %FAIL_COUNT%

if %FAIL_COUNT% gtr 0 (
    echo [ERROR] The following tests failed:%FAILED_TESTS%
    exit /b 1
) else (
    echo [SUCCESS] All bootstrap tests passed successfully!
    exit /b 0
)
