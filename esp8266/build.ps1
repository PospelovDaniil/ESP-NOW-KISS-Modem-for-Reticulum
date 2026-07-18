#!/usr/bin/env pwsh
# Build firmware for ESP8266
# Requires: ESP8266 RTOS SDK + Python
#
# Examples:
#   .\build.ps1

param([switch]$Help)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir  = Join-Path $ScriptDir "build"
$OutputDir = Join-Path $ScriptDir "firmware_build"

if ($Help) {
    Write-Host "Usage: .\build.ps1" -NoNewline
    exit 0
}

# --- Locate Python ---
$Python = $null
foreach ($p in @("E:\Python379\Python\python.exe", "python")) {
    if (Get-Command $p -ErrorAction SilentlyContinue) { $Python = $p; break }
}
if (!$Python) { throw "Python not found." }

# --- Locate ESP8266 RTOS SDK ---
$IDF_PATH = "E:\ESP8266_RTOS_SDK"
if (!(Test-Path "$IDF_PATH\tools\idf.py")) { throw "ESP8266 RTOS SDK not found at $IDF_PATH" }
$env:IDF_PATH = $IDF_PATH

# --- Export toolchain paths ---
Write-Host ">>> Setting up SDK from $IDF_PATH ..." -ForegroundColor Cyan

$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $Python "$IDF_PATH\tools\idf_tools.py" export --format key-value 2>$null |
    Where-Object { $_ -match '^[A-Za-z_][A-Za-z0-9_]*=' } |
    ForEach-Object {
        $k, $v = $_ -split '=', 2
        if ($k -eq 'PATH') {
            $env:PATH = ($v -replace [regex]::Escape('%PATH%'), $env:PATH)
        } else {
            Set-Item "Env:$k" $v
        }
    }
$ErrorActionPreference = $prevEAP

# --- Get git version tag ---
$GitVersion = (git -C $ScriptDir describe --tags --always 2>$null)
if (!$GitVersion) { $GitVersion = "unknown" }

# --- Build ---
Write-Host ">>> Building for esp8266 ..." -ForegroundColor Cyan

& $Python "$IDF_PATH\tools\idf.py" build
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# --- Copy binaries to firmware_build ---
if (Test-Path $OutputDir) { Remove-Item $OutputDir -Recurse -Force }
New-Item -ItemType Directory -Path $OutputDir | Out-Null

$Bootloader = Join-Path $BuildDir "bootloader\bootloader.bin"
$PartTable  = Join-Path $BuildDir "partition_table\partition-table.bin"
$Firmware   = Join-Path $BuildDir "kiss_modem_esp8266.bin"

foreach ($f in @($Bootloader, $PartTable, $Firmware)) {
    if (!(Test-Path $f)) { throw "Missing: $f" }
}

$BaseName = "esp-now.kiss.modem.esp8266-$GitVersion"

Copy-Item $Bootloader (Join-Path $OutputDir "$BaseName.bootloader.bin")
Copy-Item $PartTable  (Join-Path $OutputDir "$BaseName.partition-table.bin")
Copy-Item $Firmware   (Join-Path $OutputDir "$BaseName.app.bin")

# --- Create flash instruction ---
$Esptool = "$IDF_PATH\components\esptool_py\esptool\esptool.py"

$FlashCmd = @"
Flash command (run from esp8266/ directory):

  $Python $Esptool -p COM_PORT -b 460800 --after hard_reset write_flash `
    --flash_mode dio --flash_size 2MB --flash_freq 40m `
    0x00000  build\bootloader\bootloader.bin `
    0x8000   build\partition_table\partition-table.bin `
    0x10000  build\kiss_modem_esp8266.bin

Or simply:

  $Python "$IDF_PATH\tools\idf.py" -p COM_PORT flash
"@

$FlashCmd | Out-File -FilePath (Join-Path $OutputDir "command_flash.txt") -Encoding UTF8

# --- Done ---
$Files = Get-ChildItem $OutputDir
Write-Host "`n>>> Build complete: $GitVersion" -ForegroundColor Green
foreach ($f in $Files) {
    $Size = [math]::Round($f.Length / 1KB, 1)
    Write-Host "  $($f.Name)  ($Size KB)" -ForegroundColor Green
}
Write-Host "`n>>> See firmware_build\command_flash.txt for flash instructions" -ForegroundColor Cyan
