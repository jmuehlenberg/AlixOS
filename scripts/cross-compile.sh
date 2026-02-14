#!/bin/bash
# Cross-compile AlixOS kernel modules from dev machine
#
# Prerequisites:
#   - aarch64-linux-gnu-gcc toolchain installed
#   - Raspberry Pi kernel headers downloaded
#
# Usage: ./scripts/cross-compile.sh [path-to-kernel-headers]

set -e

KDIR="${1:-/opt/rpi-kernel-headers}"
CROSS="aarch64-linux-gnu-"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL_DIR="$SCRIPT_DIR/../kernel"

if [ ! -d "$KDIR" ]; then
    echo "Error: Kernel headers not found at $KDIR"
    echo ""
    echo "To get Pi kernel headers:"
    echo "  1. On the Pi: apt install linux-headers-\$(uname -r)"
    echo "  2. Copy /lib/modules/\$(uname -r)/build to your dev machine"
    echo ""
    echo "Usage: $0 /path/to/kernel/headers"
    exit 1
fi

echo "AlixOS Cross-Compile"
echo "  Kernel headers: $KDIR"
echo "  Cross compiler: ${CROSS}gcc"
echo ""

cd "$KERNEL_DIR"
make ARCH=arm64 CROSS_COMPILE="$CROSS" KDIR="$KDIR" clean
make ARCH=arm64 CROSS_COMPILE="$CROSS" KDIR="$KDIR"

echo ""
echo "Build complete. Modules:"
find . -name '*.ko' -exec ls -la {} \;
