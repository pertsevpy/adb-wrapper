$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "========================================"
Write-Host " Building ADB/Fastboot wrapper"
Write-Host "========================================"
Write-Host ""

# Проверяем наличие MSVC.
$cl = Get-Command cl.exe -ErrorAction SilentlyContinue

if (-not $cl) {
    Write-Host ""
    Write-Host "ERROR: cl.exe not found."
    Write-Host ""
    Write-Host "Open 'Developer PowerShell for VS 2026'"
    Write-Host "and run:"
    Write-Host ""
    Write-Host "    code ."
    Write-Host ""
    exit 1
}

Write-Host "Compiler:"
(cl.exe 2>&1 | Select-Object -First 1) -replace '^.*$',''

Write-Host ""
Write-Host "Cleaning old build..."

Remove-Item `
    .\wrapper.obj, `
    .\wrapper.exe, `
    .\wrapper.pdb `
    -Force `
    -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "Compiling..."

cl.exe `
    /nologo `
    /std:c++17 `
    /EHsc `
    /W4 `
    /DUNICODE `
    /D_UNICODE `
    .\wrapper.cpp `
    /link `
    /SUBSYSTEM:CONSOLE `
    /OUT:wrapper.exe

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "BUILD FAILED"
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "BUILD OK"
Write-Host ""

Write-Host "Output:"
Write-Host "    $((Resolve-Path .\wrapper.exe).Path)"
Write-Host ""

Write-Host "Next:"
Write-Host "    copy wrapper.exe adb.exe"
Write-Host "    copy wrapper.exe fastboot.exe"
Write-Host ""

Write-Host "Done."

