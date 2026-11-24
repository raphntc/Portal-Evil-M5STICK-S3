#!/usr/bin/env bash
#
# Build the Portal Evil HUEBR firmware (.bin) for the M5StickS3.
#
# Usage:  ./scripts/build.sh
# Output: firmware/portal-evil-sticks3-merged.bin  (flash at 0x0 in M5Burner)
#
# Requires arduino-cli on PATH:  https://arduino.github.io/arduino-cli/latest/installation/
# The ESP32 core and the M5Unified/M5GFX libraries are installed automatically if missing.
#
# NOTE: the M5StickS3 is a new ESP32-S3 board. If M5Stack ships a dedicated
# arduino-cli board id (e.g. esp32:esp32:m5stack_sticks3), set FQBN to it.
# Until then we build against the generic ESP32-S3 dev module with the flash/
# PSRAM/USB options that match M5Stack's PlatformIO profile for this device.
#
# USB MODE: built in USB-OTG (TinyUSB) mode (USBMode=default) so the WiFi Duck
# project can enumerate as a USB keyboard (BadUSB). The Evil Portal ignores USB
# mode, so this one build serves both projects. USBMode=hwcdc still compiles
# (the Duck's typing just becomes a no-op).

set -euo pipefail

FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc,USBMode=default"
SKETCH_DIR="portal-evil-sticks3"
SKETCH="$SKETCH_DIR/portal-evil-sticks3.ino"
ESP32_URL="https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json"
OUT_DIR="firmware"

# Run from the repo root (this script lives in scripts/).
cd "$(dirname "$0")/.."

echo "==> Checking arduino-cli..."
if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "ERROR: arduino-cli not found on PATH."
  echo "Install it: https://arduino.github.io/arduino-cli/latest/installation/"
  echo "  macOS/Linux: curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh"
  echo "  Windows:     use scripts/build.ps1 or download the binary from the page above"
  exit 1
fi

echo "==> Installing ESP32 core + M5Unified/M5GFX libraries (idempotent)..."
arduino-cli core update-index --additional-urls "$ESP32_URL"
arduino-cli core install esp32:esp32 --additional-urls "$ESP32_URL"
arduino-cli lib install M5Unified   # pulls in M5GFX as a dependency
arduino-cli lib install M5GFX
# M5PM1 (power-management helper) is optional; ignore if it is not in the index yet.
arduino-cli lib install M5PM1 || echo "   (M5PM1 not installed - fine, M5Unified handles power)"

echo "==> Compiling $SKETCH ..."
arduino-cli compile --fqbn "$FQBN" --warnings all --export-binaries "$SKETCH"

BUILD_DIR="$SKETCH_DIR/build/esp32.esp32.esp32s3"
mkdir -p "$OUT_DIR"
cp "$BUILD_DIR/portal-evil-sticks3.ino.merged.bin" "$OUT_DIR/portal-evil-sticks3-merged.bin"
cp "$BUILD_DIR/portal-evil-sticks3.ino.bin"        "$OUT_DIR/portal-evil-sticks3-app.bin"

echo ""
echo "==> Done! Flash this file in M5Burner (write at 0x0):"
echo "    $OUT_DIR/portal-evil-sticks3-merged.bin"
echo "    (or the app-only image for offset 0x10000: $OUT_DIR/portal-evil-sticks3-app.bin)"
