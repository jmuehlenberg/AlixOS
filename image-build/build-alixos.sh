#!/bin/bash
# ============================================================================
# AlixOS Image Builder
# ============================================================================
#
# Builds a ready-to-flash SD card image for PiStorm32-lite CM4 in Amiga 1200.
# Uses pi-gen (official Raspberry Pi OS image builder) with a custom AlixOS
# stage that pre-installs all drivers, desktop, WiFi, Bluetooth, etc.
#
# NOTE: pi-gen does not handle paths with spaces well (e.g., iCloud Drive).
# This script copies everything to /tmp/alixos-build for the actual build.
#
# Requirements:
#   - Docker (recommended) OR Debian/Ubuntu build host
#   - ~10 GB free disk space
#   - Internet connection (for package downloads)
#
# Usage:
#   ./build-alixos.sh              # Build with Docker (recommended)
#   ./build-alixos.sh --no-docker  # Build natively on Debian/Ubuntu
#
# Output:
#   deploy/alixos-<date>.img.xz    # Compressed SD card image
#
# ============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
USE_DOCKER=true

# Use a temp directory without spaces for the build
BUILD_DIR="/tmp/alixos-build"
PIGEN_DIR="$BUILD_DIR/pi-gen"

if [ "$1" = "--no-docker" ]; then
    USE_DOCKER=false
fi

echo "============================================"
echo "  AlixOS Image Builder"
echo "============================================"
echo ""
echo "  Project:    $PROJECT_DIR"
echo "  Build dir:  $BUILD_DIR"
echo "  Docker:     $USE_DOCKER"
echo ""

# ---- Step 1: Clone pi-gen if not present ----

mkdir -p "$BUILD_DIR"

if [ ! -d "$PIGEN_DIR" ]; then
    echo "[1/5] Cloning pi-gen..."
    git clone --depth 1 https://github.com/RPi-Distro/pi-gen.git "$PIGEN_DIR"
else
    echo "[1/5] pi-gen already present."
fi

# ---- Step 2: Write pi-gen config ----

echo "[2/5] Writing pi-gen configuration..."

cat > "$PIGEN_DIR/config" << 'PICONFIG'
IMG_NAME="alixos"
RELEASE="bookworm"
TARGET_HOSTNAME="alixos"
FIRST_USER_NAME="alix"
FIRST_USER_PASS="alix"
ENABLE_SSH=1
LOCALE_DEFAULT="en_US.UTF-8"
KEYBOARD_KEYMAP="us"
KEYBOARD_LAYOUT="English (US)"
TIMEZONE_DEFAULT="Europe/Berlin"

# Build 64-bit image for CM4
ARCH=arm64

# We need stages 0-2 (Lite) plus our custom stage
STAGE_LIST="stage0 stage1 stage2 stage-alixos"
PICONFIG

# ---- Step 3: Copy AlixOS custom stage ----

echo "[3/5] Setting up AlixOS custom stage..."

# Remove old stage if exists
rm -rf "$PIGEN_DIR/stage-alixos"

# Copy the stage definition
cp -r "$SCRIPT_DIR/stage-alixos" "$PIGEN_DIR/stage-alixos"

# Copy AlixOS project files into the stage
mkdir -p "$PIGEN_DIR/stage-alixos/01-install-alixos/files/alix"
cp -r "$PROJECT_DIR/kernel"  "$PIGEN_DIR/stage-alixos/01-install-alixos/files/alix/"
cp -r "$PROJECT_DIR/fpga"    "$PIGEN_DIR/stage-alixos/01-install-alixos/files/alix/"
cp -r "$PROJECT_DIR/system"  "$PIGEN_DIR/stage-alixos/01-install-alixos/files/alix/"
cp -r "$PROJECT_DIR/distro"  "$PIGEN_DIR/stage-alixos/01-install-alixos/files/alix/"
cp -r "$PROJECT_DIR/tools"   "$PIGEN_DIR/stage-alixos/01-install-alixos/files/alix/"
cp -r "$PROJECT_DIR/scripts" "$PIGEN_DIR/stage-alixos/01-install-alixos/files/alix/"

# ---- Step 4: Skip unwanted stages ----

# Skip stages 3-5
for stage in stage3 stage4 stage5; do
    if [ -d "$PIGEN_DIR/$stage" ]; then
        touch "$PIGEN_DIR/$stage/SKIP"
        rm -f "$PIGEN_DIR/$stage/SKIP_IMAGES" 2>/dev/null
    fi
done

# Don't export images for stages 0-2
for stage in stage0 stage1 stage2; do
    touch "$PIGEN_DIR/$stage/SKIP_IMAGES"
done

# DO export the final AlixOS stage image
rm -f "$PIGEN_DIR/stage-alixos/SKIP_IMAGES" 2>/dev/null

# ---- Step 5: Build ----

echo "[4/5] Building AlixOS image..."
echo "       This will take 20-40 minutes depending on your machine."
echo ""

cd "$PIGEN_DIR"

if [ "$USE_DOCKER" = true ]; then
    if ! command -v docker &>/dev/null; then
        echo "Error: Docker not found. Install Docker or use --no-docker on Debian/Ubuntu."
        exit 1
    fi
    ./build-docker.sh
else
    sudo ./build.sh
fi

# ---- Step 6: Copy output ----

echo "[5/5] Copying output image..."

# Copy to project directory and to build dir
OUTPUT_DIR="$SCRIPT_DIR/deploy"
mkdir -p "$OUTPUT_DIR"

IMG_FILE=$(find "$PIGEN_DIR/deploy" -name "alixos-*.img.xz" -type f 2>/dev/null | sort | tail -1)

if [ -z "$IMG_FILE" ]; then
    # Also check for uncompressed images
    IMG_FILE=$(find "$PIGEN_DIR/deploy" -name "alixos-*.img" -type f 2>/dev/null | sort | tail -1)
fi

if [ -n "$IMG_FILE" ]; then
    cp "$IMG_FILE" "$OUTPUT_DIR/"
    FINAL_IMG="$OUTPUT_DIR/$(basename "$IMG_FILE")"
    echo ""
    echo "============================================"
    echo "  AlixOS Image Built Successfully!"
    echo "============================================"
    echo ""
    echo "  Image: $FINAL_IMG"
    echo "  Size:  $(du -h "$FINAL_IMG" | cut -f1)"
    echo ""
    echo "  Flash with:"
    echo "    Etcher:     Open $FINAL_IMG and flash to SD card"
    echo "    rpi-imager: Use 'Custom Image' option"
    echo ""
    echo "  Default login: alix / alix"
    echo "  WiFi:          Configure via NetworkManager on desktop"
    echo "  Bluetooth:     Available via Blueman in system tray"
    echo ""
else
    echo "Error: No output image found!"
    echo "Check $PIGEN_DIR/work/ for build logs."
    ls -la "$PIGEN_DIR/deploy/" 2>/dev/null || echo "No deploy directory found."
    exit 1
fi
