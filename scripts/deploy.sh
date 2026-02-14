#!/bin/bash
# Deploy AlixOS files to Raspberry Pi via SSH
#
# Usage: ./scripts/deploy.sh [pi-hostname-or-ip]

set -e

PI_HOST="${1:-alixos.local}"
PI_USER="alix"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/.."

echo "AlixOS Deploy to $PI_USER@$PI_HOST"
echo ""

# Check for compiled modules
KO_FILES=$(find "$PROJECT_DIR/kernel" -name '*.ko' 2>/dev/null | wc -l)
if [ "$KO_FILES" -eq 0 ]; then
    echo "Error: No compiled kernel modules found."
    echo "Run 'make' in the kernel/ directory first."
    exit 1
fi

# Create target directory on Pi
ssh "$PI_USER@$PI_HOST" "sudo mkdir -p /lib/modules/\$(uname -r)/extra/alix"

# Copy kernel modules
echo "  Copying kernel modules..."
for ko in "$PROJECT_DIR"/kernel/common/pistorm_bus.ko \
          "$PROJECT_DIR"/kernel/keyboard/alix_kbd.ko \
          "$PROJECT_DIR"/kernel/mouse/alix_mouse.ko \
          "$PROJECT_DIR"/kernel/joystick/alix_joy.ko; do
    if [ -f "$ko" ]; then
        scp "$ko" "$PI_USER@$PI_HOST:/tmp/"
        echo "    $(basename "$ko")"
    fi
done

# Copy FPGA loader
if [ -f "$PROJECT_DIR/fpga/alix-fpga-load" ]; then
    echo "  Copying FPGA loader..."
    scp "$PROJECT_DIR/fpga/alix-fpga-load" "$PI_USER@$PI_HOST:/tmp/"
fi

# Install on Pi
echo "  Installing on Pi..."
ssh "$PI_USER@$PI_HOST" "
    # Unload old modules
    sudo rmmod alix_joy alix_mouse alix_kbd pistorm_bus 2>/dev/null || true

    # Install modules
    sudo cp /tmp/pistorm_bus.ko /lib/modules/\$(uname -r)/extra/alix/ 2>/dev/null || true
    sudo cp /tmp/alix_kbd.ko /lib/modules/\$(uname -r)/extra/alix/ 2>/dev/null || true
    sudo cp /tmp/alix_mouse.ko /lib/modules/\$(uname -r)/extra/alix/ 2>/dev/null || true
    sudo cp /tmp/alix_joy.ko /lib/modules/\$(uname -r)/extra/alix/ 2>/dev/null || true
    sudo depmod -a

    # Install FPGA loader
    if [ -f /tmp/alix-fpga-load ]; then
        sudo install -m 755 /tmp/alix-fpga-load /usr/local/bin/
    fi

    # Load modules
    sudo modprobe pistorm_bus
    sudo modprobe alix_kbd
    sudo modprobe alix_mouse
    sudo modprobe alix_joy

    echo ''
    echo 'Modules loaded:'
    lsmod | grep -E 'alix|pistorm'
"

echo ""
echo "Deploy complete."
