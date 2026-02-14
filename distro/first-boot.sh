#!/bin/bash
# AlixOS first-boot configuration script
# Run once on fresh Raspberry Pi OS Lite installation
#
# Usage: sudo bash /opt/alix/distro/first-boot.sh

set -e

echo "=== AlixOS First Boot Setup ==="

# 1. Install required packages
echo "  Installing packages..."
apt-get update
apt-get install -y \
    xserver-xorg xserver-xorg-video-fbdev xinit \
    lxde-core lightdm lightdm-gtk-greeter \
    build-essential "linux-headers-$(uname -r)" git \
    evtest xinput jstest-gtk \
    chromium-browser \
    vim htop tree \
    bluez blueman \
    network-manager network-manager-gnome wpasupplicant \
    firmware-brcm80211 ssh

# 2. Install FPGA loader
echo "  Installing FPGA loader..."
install -m 755 /opt/alix/fpga/alix-fpga-load /usr/local/bin/
mkdir -p /usr/local/share/alix
if [ -f /opt/alix/fpga/bitstream.bin ]; then
    install -m 644 /opt/alix/fpga/bitstream.bin /usr/local/share/alix/
fi

# 3. Build and install kernel modules
echo "  Building kernel modules..."
cd /opt/alix/kernel
make
make install

# 4. Install systemd services
echo "  Installing services..."
install -m 644 /opt/alix/system/alix-fpga-load.service /etc/systemd/system/
install -m 644 /opt/alix/system/alix-input.service /etc/systemd/system/
systemctl enable alix-fpga-load.service
systemctl enable alix-input.service

# 5. Install module auto-load configuration
install -m 644 /opt/alix/system/modules-load.d/alix.conf /etc/modules-load.d/

# 6. Install udev rules
install -m 644 /opt/alix/system/udev/99-alix-input.rules /etc/udev/rules.d/

# 7. Install Xorg configuration
mkdir -p /etc/X11/xorg.conf.d
install -m 644 /opt/alix/system/xorg/10-alix-input.conf /etc/X11/xorg.conf.d/

# 8. Create alix user
echo "  Creating alix user..."
if ! id alix &>/dev/null; then
    useradd -m -G sudo,video,input,audio -s /bin/bash alix
    echo "alix:alix" | chpasswd
fi

# 9. Configure auto-login
if [ -f /etc/lightdm/lightdm.conf ]; then
    sed -i 's/#autologin-user=/autologin-user=alix/' /etc/lightdm/lightdm.conf
    sed -i 's/#autologin-user-timeout=0/autologin-user-timeout=0/' /etc/lightdm/lightdm.conf
fi

# 10. Set hostname
hostnamectl set-hostname alixos

# 11. Apply boot config
echo "  Applying boot configuration..."
if [ -f /opt/alix/distro/config.txt ]; then
    cp /boot/config.txt /boot/config.txt.bak
    cp /opt/alix/distro/config.txt /boot/config.txt
fi

echo "=== AlixOS setup complete. Reboot to activate. ==="
echo "    Default user: alix / alix"
echo "    HDMI output will be used for display."
echo "    Amiga keyboard, mouse, and joystick will be active after reboot."
