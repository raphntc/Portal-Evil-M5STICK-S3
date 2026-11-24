<#
  Build the Portal Evil HUEBR firmware (.bin) for the M5StickS3 (Windows).

  Usage:  powershell -ExecutionPolicy Bypass -File scripts\build.ps1
  Output: firmware\portal-evil-sticks3-merged.bin  (flash at 0x0 in M5Burner)

  Requires arduino-cli on PATH: https://arduino.github.io/arduino-cli/latest/installation/
  The ESP32 core and the M5Unified/M5GFX libraries are installed automatically if missing.

  NOTE: the M5StickS3 is a new ESP32-S3 board. If M5Stack ships a dedicated
  arduino-cli board id (e.g. esp32:esp32:m5stack_sticks3), set $Fqbn to it.
  Until then we build against the generic ESP32-S3 dev module with the flash/
  PSRAM/USB options that match M5Stack's PlatformIO profile for this device.

  USB MODE: we build in USB-OTG (TinyUSB) mode (USBMode=default) so the WiFi
  Duck project can enumerate as a USB keyboard (BadUSB). The Evil Portal does
  not care about USB mode, so this single build serves both projects. If you
  only want the Evil Portal you may switch to USBMode=hwcdc; the firmware still
  compiles (the Duck's typing just becomes a no-op).
#>

$ErrorActionPreference = "Stop"

$Fqbn      = "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc,USBMode=default"
$SketchDir = "portal-evil-sticks3"
$Sketch    = "$SketchDir/portal-evil-sticks3.ino"
$Esp32Url  = "https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json"
$OutDir    = "firmware"

# Run from the repo root (this script lives in scripts\).
Set-Location (Join-Path $PSScriptRoot "..")

Write-Host "==> Checking arduino-cli..."
if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
  Write-Host "ERROR: arduino-cli not found on PATH." -ForegroundColor Red
  Write-Host "Install it: https://arduino.github.io/arduino-cli/latest/installation/"
  exit 1
}

Write-Host "==> Installing ESP32 core + M5Unified/M5GFX libraries (idempotent)..."
arduino-cli core update-index --additional-urls $Esp32Url
arduino-cli core install esp32:esp32 --additional-urls $Esp32Url
arduino-cli lib install M5Unified   # pulls in M5GFX as a dependency
arduino-cli lib install M5GFX
# M5PM1 (power-management helper) is optional; ignore if it is not in the index yet.
try { arduino-cli lib install M5PM1 } catch { Write-Host "   (M5PM1 not installed - fine, M5Unified handles power)" }

Write-Host "==> Compiling $Sketch ..."
arduino-cli compile --fqbn $Fqbn --warnings all --export-binaries $Sketch

$BuildDir = "$SketchDir/build/esp32.esp32.esp32s3"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Copy-Item "$BuildDir/portal-evil-sticks3.ino.merged.bin" "$OutDir/portal-evil-sticks3-merged.bin" -Force
Copy-Item "$BuildDir/portal-evil-sticks3.ino.bin"        "$OutDir/portal-evil-sticks3-app.bin"    -Force

Write-Host ""
Write-Host "==> Done! Flash this file in M5Burner (write at 0x0):" -ForegroundColor Green
Write-Host "    $OutDir\portal-evil-sticks3-merged.bin"
Write-Host "    (or the app-only image for offset 0x10000: $OutDir\portal-evil-sticks3-app.bin)"
