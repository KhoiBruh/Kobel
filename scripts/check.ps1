# Kobel gate runner - the four checks that must be green before a commit.
#
#   powershell -File scripts/check.ps1              # build -> test -> snap -> fixpoint
#   powershell -File scripts/check.ps1 -Update      # ... accepting the current C output
#
# Every gate uses the compiler in $Kobel (default: build/kobel.exe), so rebuild
# that binary first if you changed the C++/seed path. Before committing a source
# change, also regenerate the seed:  build/kobel.exe src/main.kb -emit-c dist/bootstrap.c

param(
    [switch]$Update,
    [string]$Kobel = "build/kobel.exe"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Kobel)) {
    Write-Host "[ERROR] compiler not found: $Kobel" -ForegroundColor Red
    Write-Host "        build it first (e.g. scripts\build_seed.bat, then .\build\seed\kobel_seed.exe build)"
    exit 1
}

function Invoke-Gate {
    param([string]$Label, [string[]]$GateArgs)
    Write-Host ""
    Write-Host "=== $Label" -ForegroundColor Cyan
    & $Kobel @GateArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "[FAILED] $Label (exit $LASTEXITCODE)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

Invoke-Gate "build"     @("build")
Invoke-Gate "test"      @("test")

if ($Update) {
    Invoke-Gate "snap --update" @("snap", "--update")
}
Invoke-Gate "snap"      @("snap")
Invoke-Gate "fixpoint"  @("fixpoint")

Write-Host ""
Write-Host "[OK] all gates green: build, test, snap, fixpoint" -ForegroundColor Green
Write-Host "     remember to regenerate dist/bootstrap.c and commit tests/golden with a deliberate C change."
