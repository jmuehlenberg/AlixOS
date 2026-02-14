#!/bin/bash -e
# ============================================================================
# AlixOS Configuration Stage
# ============================================================================
# Configures desktop, auto-login, WiFi, Bluetooth, boot config.

echo "AlixOS: Configuring system..."

# ---- Boot configuration ----

# Apply AlixOS boot config
install -m 755 files/config.txt "${ROOTFS_DIR}/boot/firmware/config.txt" 2>/dev/null || \
install -m 755 files/config.txt "${ROOTFS_DIR}/boot/config.txt" 2>/dev/null || true

on_chroot << 'CHEOF'

# ---- Desktop auto-login ----

# Configure LightDM for auto-login
LIGHTDM_CONF="/etc/lightdm/lightdm.conf"
if [ -f "$LIGHTDM_CONF" ]; then
    sed -i 's/^#\?autologin-user=.*/autologin-user=alix/' "$LIGHTDM_CONF"
    sed -i 's/^#\?autologin-user-timeout=.*/autologin-user-timeout=0/' "$LIGHTDM_CONF"
else
    # Create minimal lightdm config
    mkdir -p /etc/lightdm
    cat > "$LIGHTDM_CONF" << 'LIGHTDM'
[Seat:*]
autologin-user=alix
autologin-user-timeout=0
user-session=LXDE
greeter-session=lightdm-gtk-greeter
LIGHTDM
fi

# Ensure alix user is in autologin group
groupadd -f autologin
usermod -a -G autologin alix 2>/dev/null || true

# ---- WiFi & Bluetooth ----

# Enable NetworkManager (handles WiFi)
systemctl enable NetworkManager || true

# Enable Bluetooth
systemctl enable bluetooth || true

# Enable SSH
systemctl enable ssh || true

# ---- Default desktop session ----

# Set LXDE as default session for alix user
mkdir -p /home/alix/.config
cat > /home/alix/.dmrc << 'DMRC'
[Desktop]
Session=LXDE
DMRC
chown alix:alix /home/alix/.dmrc

# ---- NetworkManager WiFi configuration ----

# Ensure NetworkManager manages WiFi (not dhcpcd)
cat > /etc/NetworkManager/NetworkManager.conf << 'NMCONF'
[main]
plugins=ifupdown,keyfile
dhcp=internal

[ifupdown]
managed=true

[device]
wifi.scan-rand-mac-address=no
NMCONF

# Disable dhcpcd to avoid conflicts with NetworkManager
systemctl disable dhcpcd 2>/dev/null || true

# ---- PulseAudio for HDMI audio ----

# Set HDMI as default audio output
mkdir -p /home/alix/.config/pulse
cat > /home/alix/.config/pulse/default.pa << 'PULSE'
.include /etc/pulse/default.pa
set-default-sink alsa_output.platform-bcm2835_audio.stereo-fallback
PULSE
chown -R alix:alix /home/alix/.config/pulse

# ---- System branding ----

# Set OS release info
cat > /etc/os-release << 'OSREL'
PRETTY_NAME="AlixOS (based on Raspberry Pi OS)"
NAME="AlixOS"
VERSION_ID="1.0"
VERSION="1.0 (Amiga)"
ID=alixos
ID_LIKE=debian
HOME_URL="https://github.com/alixos"
SUPPORT_URL="https://github.com/alixos/issues"
OSREL

# Set issue banner
cat > /etc/issue << 'ISSUE'

    _    _ _       ___  ____
   / \  | (_)_  __/ _ \/ ___|
  / _ \ | | \ \/ / | | \___ \
 / ___ \| | |>  <| |_| |___) |
/_/   \_\_|_/_/\_\\___/|____/

Linux on Amiga 1200 via PiStorm32-lite

ISSUE

CHEOF

echo "AlixOS: Configuration complete."
