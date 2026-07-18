#!/usr/bin/env pwsh
# Build firmware for all supported ESP32 variants
# Requires: ESP-IDF environment (idf.py, esptool.py in PATH)

param([switch]$Help)

if ($Help) {
    Write-Host @"
Usage:
  .\build-all.ps1          # builds esp32, esp32s2, esp32s3, esp32c2, esp32c3
Output: firmware_build/esp-now.kiss.modem.<target>-<ver>.bin
"@
    exit 0
}

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Targets   = @("esp32", "esp32s2", "esp32s3", "esp32c2", "esp32c3")

foreach ($Target in $Targets) {
    Write-Host "`n========================================" -ForegroundColor Yellow
    Write-Host ">>> Building $Target" -ForegroundColor Yellow
    Write-Host "========================================`n" -ForegroundColor Yellow

    & "$ScriptDir\build.ps1" -Target $Target
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $Target" }
}

Write-Host "`n>>> All targets built:" -ForegroundColor Green
$OutputDir = Join-Path $ScriptDir "firmware_build"
foreach ($Target in $Targets) {
    $Bin = Get-ChildItem (Join-Path $OutputDir "esp-now.kiss.modem.$Target-*.bin") -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($Bin) {
        $Size = [math]::Round($Bin.Length / 1KB, 1)
        Write-Host "  $($Bin.Name)  ($Size KB)" -ForegroundColor Green
    }
}
