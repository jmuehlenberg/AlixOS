# AlixOS

**Custom Linux for PiStorm32-lite on Amiga 1200**

AlixOS turns a Raspberry Pi CM4 with PiStorm32-lite into a full Linux companion system inside an Amiga 1200 — with native access to the Amiga's keyboard, mouse, joystick, serial port, parallel port, floppy drive, power LED and real-time clock.

## Features

- **Amiga Keyboard** — Full keyboard input via CIA-A serial data register
- **Amiga Mouse** — 3-button mouse via JOY0DAT quadrature counters + POTGOR
- **Joystick & CD32 Gamepad** — Digital joystick and 7-button CD32 gamepad with shift register protocol
- **Serial Port** — Full TTY device (`/dev/ttyAMIGA0`) with baud rate config and handshaking
- **Parallel Port** — Bidirectional Centronics port (`/dev/alix-parport`) with BUSY/strobe
- **Power LED** — Linux LED subsystem with heartbeat and disk-activity triggers
- **Real-Time Clock** — Ricoh RP5C01A battery-backed RTC for timekeeping without network
- **Floppy Drive** — Read and write Amiga DD disks as ADF images with MFM encoding
- **FPGA Loader** — Automatic PiStorm32-lite FPGA bitstream loading at boot
- **Desktop** — LXDE desktop with Chromium, WiFi, Bluetooth, SSH

## Architecture

```
Amiga 1200 Hardware
        |
   PiStorm32-lite FPGA
        |
   Raspberry Pi CM4 (aarch64)
        |
   AlixOS (Debian Trixie)
        |
   ┌────┴─────────────────────────────────┐
   │  pistorm_bus  — GPIO ↔ Amiga Bus     │
   │  alix_kbd     — Keyboard             │
   │  alix_mouse   — Mouse                │
   │  alix_joy     — Joystick / CD32 Pad  │
   │  alix_serial  — Serial (ttyAMIGA0)   │
   │  alix_parallel — Parallel Port       │
   │  alix_led     — Power LED            │
   │  alix_rtc     — Real-Time Clock      │
   │  alix_floppy  — DD Floppy (ADF)      │
   └──────────────────────────────────────┘
```

## Project Structure

```
AlixOS/
├── kernel/                     Linux kernel modules
│   ├── common/                 PiStorm32-lite bus driver (GPIO ↔ Amiga bus)
│   ├── keyboard/               CIA-A keyboard driver
│   ├── mouse/                  JOY0DAT mouse driver
│   ├── joystick/               Joystick + CD32 gamepad driver
│   ├── serial/                 Paula serial port (ttyAMIGA0)
│   ├── parallel/               CIA-A parallel port
│   ├── led/                    Power LED control
│   ├── rtc/                    RP5C01A real-time clock
│   └── floppy/                 DD floppy with MFM codec
├── fpga/                       FPGA bitstream loader
├── tools/                      Userspace tools (floppy CLI)
├── system/                     systemd services, udev rules, Xorg config
├── distro/                     Boot config, package lists
├── image-build/                pi-gen based image builder
└── scripts/                    Cross-compile and deploy helpers
```

## Quick Start

### Build the SD card image

Requires Docker and ~10 GB free disk space.

```bash
cd image-build
chmod +x build-alixos.sh
./build-alixos.sh
```

The finished image will be at `image-build/deploy/alixos-<date>.img.xz`.

### Flash to SD card

```bash
xz -d image-build/deploy/alixos-*.img.xz
sudo dd if=image-build/deploy/alixos-*.img of=/dev/sdX bs=4M status=progress
```

Or use [Raspberry Pi Imager](https://www.raspberrypi.com/software/) / [Etcher](https://etcher.balena.io/) with the "Custom Image" option.

### Boot

1. Insert SD card into the CM4 slot on the PiStorm32-lite
2. Power on the Amiga 1200
3. LXDE desktop appears after ~30-60 seconds

**Login:** `alix` / `alix` (auto-login enabled)

## Usage Examples

### Floppy Disk Tool

```bash
# Read a disk to ADF file
floppy read mygame.adf

# Write an ADF back to disk
floppy write mygame.adf

# Verify disk against ADF
floppy verify mygame.adf

# Show disk info
floppy info mygame.adf
```

### Serial Port

```bash
# Connect at 9600 baud
minicom -D /dev/ttyAMIGA0 -b 9600
```

### Power LED

```bash
# Heartbeat blink
echo heartbeat > /sys/class/leds/amiga::power/trigger

# Disk activity indicator
echo disk-activity > /sys/class/leds/amiga::power/trigger
```

### CD32 Gamepad

```bash
# Load joystick module in CD32 mode
sudo modprobe alix_joy cd32=1

# Test 7-button gamepad
jstest /dev/input/alix-joy
```

## FPGA Bitstream

The PiStorm32-lite FPGA bitstream file is **not included** in this repository. You need to obtain it from the [PiStorm project](https://github.com/captain-amygdala/pistorm) and place it at:

```
fpga/bitstream.bin
```

The image will build without it. You can also copy it to the SD card afterwards at `/usr/local/share/alix/bitstream.bin`.

## Requirements

- **Hardware:** Amiga 1200 + PiStorm32-lite + Raspberry Pi CM4
- **Build host:** Any Linux x86_64 system with Docker
- **Disk space:** ~10 GB for the build process

## Switching Back to Emu68

Simply swap the SD card. AlixOS does not modify the PiStorm hardware — everything is software on the SD card.

## License

GPL-2.0 — see [LICENSE](LICENSE)
