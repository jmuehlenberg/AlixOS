#!/bin/bash -e
# ============================================================================
# AlixOS Installation Stage
# ============================================================================
# Copies AlixOS project files into the image and builds kernel modules.

echo "AlixOS: Installing project files..."

# Copy AlixOS project to /opt/alix in the image
install -d "${ROOTFS_DIR}/opt/alix"
cp -r files/alix/* "${ROOTFS_DIR}/opt/alix/"

# Install FPGA loader binary (will be compiled on first boot if not cross-compiled)
install -d "${ROOTFS_DIR}/usr/local/bin"
install -d "${ROOTFS_DIR}/usr/local/share/alix"

# Install systemd services
install -m 644 files/alix/system/alix-fpga-load.service \
    "${ROOTFS_DIR}/etc/systemd/system/"
install -m 644 files/alix/system/alix-input.service \
    "${ROOTFS_DIR}/etc/systemd/system/"

# Install modules-load configuration
install -d "${ROOTFS_DIR}/etc/modules-load.d"
install -m 644 files/alix/system/modules-load.d/alix.conf \
    "${ROOTFS_DIR}/etc/modules-load.d/"

# Install udev rules
install -d "${ROOTFS_DIR}/etc/udev/rules.d"
install -m 644 files/alix/system/udev/99-alix-input.rules \
    "${ROOTFS_DIR}/etc/udev/rules.d/"

# Install Xorg input configuration
install -d "${ROOTFS_DIR}/etc/X11/xorg.conf.d"
install -m 644 files/alix/system/xorg/10-alix-input.conf \
    "${ROOTFS_DIR}/etc/X11/xorg.conf.d/"

# Disable apt-listchanges (causes hours of timeouts during image export)
on_chroot << 'CHEOF'
apt-get -y remove apt-listchanges 2>/dev/null || true
rm -f /etc/apt/listchanges.conf
CHEOF

# Build kernel modules inside chroot
on_chroot << 'CHEOF'
echo "AlixOS: Building kernel modules..."

# Find the kernel version available in the image
KVER=$(ls /lib/modules/ | head -1)
KDIR="/lib/modules/${KVER}/build"

if [ -d "$KDIR" ]; then
    cd /opt/alix/kernel
    make KDIR="$KDIR" || echo "Warning: Kernel module build failed. Modules can be built on first boot."
    make KDIR="$KDIR" install || echo "Warning: Module install failed."
    depmod -a "$KVER" || true
else
    echo "Warning: Kernel headers not found for $KVER"
    echo "Kernel modules will need to be built on first boot."
    echo "Run: cd /opt/alix/kernel && make && sudo make install"
fi

# Build FPGA loader
cd /opt/alix/fpga
make || echo "Warning: FPGA loader build failed. Build on first boot with: cd /opt/alix/fpga && make && sudo make install"

if [ -f /opt/alix/fpga/alix-fpga-load ]; then
    install -m 755 /opt/alix/fpga/alix-fpga-load /usr/local/bin/
fi

# Install FPGA bitstream if provided
if [ -f /opt/alix/fpga/bitstream.bin ]; then
    echo "AlixOS: Installing FPGA bitstream..."
    install -m 644 /opt/alix/fpga/bitstream.bin /usr/local/share/alix/bitstream.bin
else
    echo "AlixOS: No FPGA bitstream found at fpga/bitstream.bin — skipping."
    echo "        Copy it later to /usr/local/share/alix/bitstream.bin"
fi

# Enable systemd services
systemctl enable alix-fpga-load.service || true
systemctl enable alix-input.service || true
CHEOF

echo "AlixOS: Installation complete."
