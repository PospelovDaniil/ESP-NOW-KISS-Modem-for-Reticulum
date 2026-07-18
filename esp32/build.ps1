#!/usr/bin/env pwsh
# Build firmware and merge into single flashable binary
# Requires: ESP-IDF environment (idf.py, esptool.py in PATH)
#
# Examples:
#   .\build.ps1                       # esp32 -> esp-now.kiss.modem.esp32.bin
#   .\build.ps1 -Target esp32s3       # esp32s3 -> esp-now.kiss.modem.esp32s3.bin
#   .\build.ps1 -Target esp32c3 -Output custom.bin

param(
    [switch]$Help,

    [Parameter(Mandatory=$false)]
    [ValidateSet("esp32","esp32s2","esp32s3","esp32c2","esp32c3")]
    [string]$Target = "esp32",

    [string]$Output = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir  = Join-Path $ScriptDir "build"
$OutputDir = Join-Path $ScriptDir "firmware_build"

if ($Help) {
    Write-Host @"
Usage:
  .\build.ps1                       # esp32 -> firmware_build/esp-now.kiss.modem.esp32-<ver>.bin
  .\build.ps1 -Target esp32s3       # esp32s3
  .\build.ps1 -Target esp32c3 -Output my.bin  # custom output
Targets: esp32, esp32s2, esp32s3, esp32c2, esp32c3
"@
    exit 0
}

# Get git version tag (e.g. v0.2-15-g7271751)
$GitVersion = (git -C $ScriptDir describe --tags --always 2>$null)
if (!$GitVersion) { $GitVersion = "unknown" }

# Auto-detect ESP-IDF environment
if (!(Get-Command "idf.py" -ErrorAction SilentlyContinue)) {
    $exported = $false

    # Try IDF_PATH/export.ps1
    if ($env:IDF_PATH -and (Test-Path "$env:IDF_PATH\export.ps1")) {
        Write-Host ">>> Sourcing ESP-IDF from IDF_PATH..." -ForegroundColor Cyan
        & "$env:IDF_PATH\export.ps1"
        $exported = $?
    }

    # Try common install locations
    if (!$exported) {
        $candidates = @(
            "C:\Espressif\esp-idf\export.ps1",
            "C:\Espressif\frameworks\esp-idf-*/export.ps1"
        )
        foreach ($c in $candidates) {
            $found = Get-Item $c -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($found) {
                Write-Host ">>> Sourcing ESP-IDF from $($found.FullName)..." -ForegroundColor Cyan
                & $found.FullName
                $exported = $?
                break
            }
        }
    }

    if (!$exported -or !(Get-Command "idf.py" -ErrorAction SilentlyContinue)) {
        throw "idf.py not found. Run from ESP-IDF terminal or set IDF_PATH."
    }
}

if (!$Output) { $Output = Join-Path $OutputDir "esp-now.kiss.modem.$Target-$GitVersion.bin" }
if (!(Test-Path $OutputDir)) { New-Item -ItemType Directory -Path $OutputDir | Out-Null }

Write-Host ">>> Building for $Target ..." -ForegroundColor Cyan

# Reset IDF_TARGET env and clean build dir to avoid target mismatch
$env:IDF_TARGET = $Target
if (Test-Path $BuildDir) { Remove-Item $BuildDir -Recurse -Force }

idf.py set-target $Target
if ($LASTEXITCODE -ne 0) { throw "set-target failed" }

idf.py build
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$Bootloader   = Join-Path $BuildDir "bootloader\bootloader.bin"
$PartTable    = Join-Path $BuildDir "partition_table\partition-table.bin"
$Firmware     = Join-Path $BuildDir "kiss_modem_esp32.bin"

foreach ($f in @($Bootloader, $PartTable, $Firmware)) {
    if (!(Test-Path $f)) { throw "Missing: $f" }
}

Write-Host ">>> Merging binaries -> $Output" -ForegroundColor Cyan

python -m esptool --chip $Target merge_bin `
    --flash_mode dio `
    --flash_size 4MB `
    --flash_freq 40m `
    -o $Output `
    0x1000   $Bootloader `
    0x8000   $PartTable `
    0x10000  $Firmware

if ($LASTEXITCODE -ne 0) { throw "merge_bin failed" }

$Size = (Get-Item $Output).Length / 1KB
Write-Host ">>> Done: $Output ($([math]::Round($Size, 1)) KB)" -ForegroundColor Green
