#!/bin/bash
# ============================================================================
# AlixOS Image Builder
# ============================================================================
#
# Builds a ready-to-flash SD card image for PiStorm32-lite CM4 in Amiga 1200.
# Uses pi-gen (official Raspberry Pi OS image builder) with AlixOS steps
# injected into stage2, so rootfs propagation works out of the box.
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

# Log file in the project directory (accessible from any environment)
LOG_FILE="$SCRIPT_DIR/build.log"

if [ "$1" = "--no-docker" ]; then
    USE_DOCKER=false
fi

# ---- Logging setup ----

log() {
    local msg="[$(date '+%H:%M:%S')] $*"
    echo "$msg"
    echo "$msg" >> "$LOG_FILE"
}

log_section() {
    echo "" >> "$LOG_FILE"
    echo "================================================================" >> "$LOG_FILE"
    log "$*"
    echo "================================================================" >> "$LOG_FILE"
}

# Start fresh log
echo "AlixOS Build Log — $(date)" > "$LOG_FILE"
echo "Host: $(uname -a)" >> "$LOG_FILE"
echo "Docker: $USE_DOCKER" >> "$LOG_FILE"
echo "" >> "$LOG_FILE"

echo "============================================"
echo "  AlixOS Image Builder"
echo "============================================"
echo ""
echo "  Project:    $PROJECT_DIR"
echo "  Build dir:  $BUILD_DIR"
echo "  Docker:     $USE_DOCKER"
echo "  Log:        $LOG_FILE"
echo ""

# ---- Step 1: Clone pi-gen if not present ----

log_section "Step 1: Clone pi-gen"

mkdir -p "$BUILD_DIR"

if [ ! -d "$PIGEN_DIR" ]; then
    log "Cloning pi-gen (arm64 branch)..."
    git clone --depth 1 --branch arm64 https://github.com/RPi-Distro/pi-gen.git "$PIGEN_DIR" 2>&1 | tee -a "$LOG_FILE"
else
    log "pi-gen already present at $PIGEN_DIR"
fi

# Log pi-gen state
log "pi-gen branch: $(git -C "$PIGEN_DIR" branch --show-current 2>/dev/null || echo 'unknown')"
log "pi-gen commit: $(git -C "$PIGEN_DIR" log --oneline -1 2>/dev/null || echo 'unknown')"

# Log existing stage structure
log "pi-gen stages found:"
for d in "$PIGEN_DIR"/stage*; do
    if [ -d "$d" ]; then
        stage_name=$(basename "$d")
        substages=$(ls -d "$d"/*/ 2>/dev/null | wc -l)
        log "  $stage_name ($substages substages)"
    fi
done

# ---- Step 2: Write pi-gen config ----

log_section "Step 2: Write pi-gen config"

cat > "$PIGEN_DIR/config" << 'PICONFIG'
IMG_NAME="alixos"
RELEASE="trixie"
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
PICONFIG

log "Config written. Contents:"
cat "$PIGEN_DIR/config" >> "$LOG_FILE"

# ---- Step 3: Inject AlixOS into stage2 ----

log_section "Step 3: Inject AlixOS substages into stage2"

# Find the highest existing substage number in stage2
LAST_SUBSTAGE=$(ls -d "$PIGEN_DIR/stage2"/*/ 2>/dev/null | sort | tail -1 | xargs basename | cut -d'-' -f1)
log "Last existing stage2 substage: $LAST_SUBSTAGE"

# Calculate next substage numbers
NEXT_NUM=$((10#$LAST_SUBSTAGE + 1))
DEPS_NUM=$(printf "%02d" $NEXT_NUM)
INSTALL_NUM=$(printf "%02d" $((NEXT_NUM + 1)))
CONFIG_NUM=$(printf "%02d" $((NEXT_NUM + 2)))

log "AlixOS substages will be: ${DEPS_NUM}-alixos-deps, ${INSTALL_NUM}-alixos-install, ${CONFIG_NUM}-alixos-configure"

# --- Substage: AlixOS dependencies ---

DEPS_DIR="$PIGEN_DIR/stage2/${DEPS_NUM}-alixos-deps"
mkdir -p "$DEPS_DIR"
log "Creating $DEPS_DIR"

# Copy package list
cp "$SCRIPT_DIR/stage-alixos/00-install-deps/00-packages" "$DEPS_DIR/00-packages"
log "  Copied 00-packages ($(wc -l < "$DEPS_DIR/00-packages") lines)"

# Copy kernel headers install script
cp "$SCRIPT_DIR/stage-alixos/00-install-deps/00-run.sh" "$DEPS_DIR/01-run.sh"
chmod +x "$DEPS_DIR/01-run.sh"
log "  Copied 01-run.sh (kernel headers)"

# --- Substage: AlixOS installation ---

INSTALL_DIR="$PIGEN_DIR/stage2/${INSTALL_NUM}-alixos-install"
mkdir -p "$INSTALL_DIR/files/alix"
log "Creating $INSTALL_DIR"

# Copy AlixOS project files
for component in kernel fpga system distro tools scripts; do
    if [ -d "$PROJECT_DIR/$component" ]; then
        cp -r "$PROJECT_DIR/$component" "$INSTALL_DIR/files/alix/"
        file_count=$(find "$PROJECT_DIR/$component" -type f | wc -l)
        log "  Copied $component/ ($file_count files)"
    else
        log "  WARNING: $PROJECT_DIR/$component not found!"
    fi
done

# Copy install script
cp "$SCRIPT_DIR/stage-alixos/01-install-alixos/00-run.sh" "$INSTALL_DIR/00-run.sh"
chmod +x "$INSTALL_DIR/00-run.sh"
log "  Copied 00-run.sh (install script)"

# --- Substage: AlixOS configuration ---

CONFIG_DIR="$PIGEN_DIR/stage2/${CONFIG_NUM}-alixos-configure"
mkdir -p "$CONFIG_DIR/files"
log "Creating $CONFIG_DIR"

# Copy boot config
cp "$SCRIPT_DIR/stage-alixos/02-configure/files/config.txt" "$CONFIG_DIR/files/config.txt"
log "  Copied files/config.txt"

# Copy configure script
cp "$SCRIPT_DIR/stage-alixos/02-configure/00-run.sh" "$CONFIG_DIR/00-run.sh"
chmod +x "$CONFIG_DIR/00-run.sh"
log "  Copied 00-run.sh (configure script)"

# ---- Step 4: Configure stage exports ----

log_section "Step 4: Configure stage exports"

# Skip stages 3-5 completely (both build and image export)
for stage in stage3 stage4 stage5; do
    if [ -d "$PIGEN_DIR/$stage" ]; then
        touch "$PIGEN_DIR/$stage/SKIP"
        touch "$PIGEN_DIR/$stage/SKIP_IMAGES"
        rm -f "$PIGEN_DIR/$stage/EXPORT_IMAGE" 2>/dev/null
        log "Marked $stage as SKIP + SKIP_IMAGES"
    fi
done

# Don't export images for stages 0-1
for stage in stage0 stage1; do
    touch "$PIGEN_DIR/$stage/SKIP_IMAGES"
    log "Marked $stage SKIP_IMAGES"
done

# DO export stage2 (which now contains AlixOS)
touch "$PIGEN_DIR/stage2/EXPORT_IMAGE"
rm -f "$PIGEN_DIR/stage2/SKIP_IMAGES" 2>/dev/null
log "Marked stage2 for EXPORT_IMAGE"

# Log final stage2 structure
log ""
log "Final stage2 structure:"
for d in "$PIGEN_DIR/stage2"/*/; do
    substage=$(basename "$d")
    files=$(ls "$d" 2>/dev/null | tr '\n' ' ')
    log "  $substage: $files"
done

# ---- Step 5: Build ----

log_section "Step 5: Build image"

log "Starting build at $(date)"
echo ""
echo "[4/5] Building AlixOS image..."
echo "       This will take 20-40 minutes depending on your machine."
echo "       Log: $LOG_FILE"
echo ""

cd "$PIGEN_DIR"

BUILD_START=$(date +%s)

if [ "$USE_DOCKER" = true ]; then
    if ! command -v docker &>/dev/null; then
        log "ERROR: Docker not found!"
        echo "Error: Docker not found. Install Docker or use --no-docker on Debian/Ubuntu."
        exit 1
    fi
    log "Running build-docker.sh..."
    ./build-docker.sh 2>&1 | tee -a "$LOG_FILE"
    BUILD_EXIT=${PIPESTATUS[0]}
else
    log "Running build.sh (native)..."
    sudo ./build.sh 2>&1 | tee -a "$LOG_FILE"
    BUILD_EXIT=${PIPESTATUS[0]}
fi

BUILD_END=$(date +%s)
BUILD_DURATION=$(( (BUILD_END - BUILD_START) / 60 ))
log "Build finished in ${BUILD_DURATION} minutes (exit code: $BUILD_EXIT)"

# Note: build-docker.sh may exit non-zero due to stage4 export even though
# the stage2 image was built successfully. We check for the image below.
if [ "$BUILD_EXIT" -ne 0 ]; then
    log "WARNING: build-docker.sh exited with code $BUILD_EXIT (checking for image anyway...)"
fi

# ---- Step 6: Copy output ----

log_section "Step 6: Copy output"

OUTPUT_DIR="$SCRIPT_DIR/deploy"
mkdir -p "$OUTPUT_DIR"

if [ "$USE_DOCKER" = true ]; then
    # In Docker mode, the image is inside the container — copy it out
    log "Copying deploy directory from Docker container..."
    CONTAINER_DEPLOY="/tmp/alixos-container-deploy"
    rm -rf "$CONTAINER_DEPLOY"
    docker cp pigen_work:/pi-gen/deploy/ "$CONTAINER_DEPLOY" 2>&1 | tee -a "$LOG_FILE"

    if [ -d "$CONTAINER_DEPLOY" ]; then
        log "Container deploy contents:"
        ls -lh "$CONTAINER_DEPLOY/" >> "$LOG_FILE" 2>&1

        # Copy all files to output directory
        cp "$CONTAINER_DEPLOY"/* "$OUTPUT_DIR/" 2>/dev/null
        # Fix ownership
        chown "$(id -u):$(id -g)" "$OUTPUT_DIR"/* 2>/dev/null || true

        SEARCH_DIR="$OUTPUT_DIR"
    else
        log "ERROR: Could not copy from container"
        SEARCH_DIR="$PIGEN_DIR/deploy"
    fi
else
    SEARCH_DIR="$PIGEN_DIR/deploy"
fi

# Find the image (zip, img.xz, or img)
log "Looking for output images in $SEARCH_DIR/"
ls -la "$SEARCH_DIR/" 2>/dev/null >> "$LOG_FILE" || log "No deploy directory found"

IMG_FILE=$(find "$SEARCH_DIR" -name "*alixos*.zip" -type f 2>/dev/null | sort | tail -1)

if [ -z "$IMG_FILE" ]; then
    IMG_FILE=$(find "$SEARCH_DIR" -name "*alixos*.img.xz" -type f 2>/dev/null | sort | tail -1)
fi

if [ -z "$IMG_FILE" ]; then
    IMG_FILE=$(find "$SEARCH_DIR" -name "*alixos*.img" -type f 2>/dev/null | sort | tail -1)
fi

if [ -z "$IMG_FILE" ]; then
    IMG_FILE=$(find "$SEARCH_DIR" -name "*.zip" -o -name "*.img*" -type f 2>/dev/null | sort | tail -1)
    log "No alixos-* image found, checking for any image: $IMG_FILE"
fi

# If image was found in container deploy, copy to final location
if [ -n "$IMG_FILE" ] && [ "$SEARCH_DIR" != "$OUTPUT_DIR" ]; then
    cp "$IMG_FILE" "$OUTPUT_DIR/"
    IMG_FILE="$OUTPUT_DIR/$(basename "$IMG_FILE")"
fi

if [ -n "$IMG_FILE" ]; then
    FINAL_IMG="$IMG_FILE"
    log "Image: $FINAL_IMG"
    log "Image size: $(du -h "$FINAL_IMG" | cut -f1)"
    echo ""
    echo "============================================"
    echo "  AlixOS Image Built Successfully!"
    echo "============================================"
    echo ""
    echo "  Image: $FINAL_IMG"
    echo "  Size:  $(du -h "$FINAL_IMG" | cut -f1)"
    echo "  Time:  ${BUILD_DURATION} minutes"
    echo ""
    echo "  Flash with:"
    echo "    Etcher:     Open $FINAL_IMG and flash to SD card"
    echo "    rpi-imager: Use 'Custom Image' option"
    echo ""
    echo "  Default login: alix / alix"
    echo "  WiFi:          Configure via NetworkManager on desktop"
    echo "  Bluetooth:     Available via Blueman in system tray"
    echo ""
    echo "  Cleanup (to free disk space):"
    echo "    sudo docker rm -v pigen_work       # Remove build container"
    echo "    sudo docker rmi pi-gen              # Remove build image"
    echo "    rm -rf /tmp/alixos-build            # Remove temp build files"
    echo ""
else
    log "ERROR: No output image found!"
    log "Contents of deploy dir:"
    find "$SEARCH_DIR" -type f 2>/dev/null >> "$LOG_FILE" || true
    log "Contents of work dir:"
    find "$PIGEN_DIR/work" -maxdepth 3 -type d 2>/dev/null >> "$LOG_FILE" || true
    echo ""
    echo "Error: No output image found!"
    echo "Check the log: $LOG_FILE"
    exit 1
fi

# Clean up container deploy temp dir
rm -rf "$CONTAINER_DEPLOY" 2>/dev/null

log "Build complete."
