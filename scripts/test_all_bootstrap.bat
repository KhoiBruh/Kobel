@echo off
setlocal enabledelayedexpansion

echo ============================================================
echo      Running Standardized Kobel Test Suite
echo ============================================================

set "FAILED_TESTS="
set "PASS_COUNT=0"
set "FAIL_COUNT=0"

set "TEST_FILES=tests\unit\test_lexer.kb tests\unit\test_ast.kb tests\unit\test_parser.kb tests\unit\test_sema_types.kb tests\unit\test_sema_symbol.kb tests\unit\test_sema_decl.kb tests\unit\test_sema_body.kb tests\unit\test_codegen.kb tests\unit\test_fmt.kb tests\features\test_syntax.kb tests\features\test_control_flow.kb tests\features\test_collections.kb tests\features\test_types.kb tests\features\test_pattern_matching.kb tests\stdlib\test_std.kb"

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
    echo [SUCCESS] All standardized tests passed successfully!
    exit /b 0
)
