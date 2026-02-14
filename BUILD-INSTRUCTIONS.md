# AlixOS — Build-Anleitung

## Voraussetzungen

- Debian 12 (Bookworm) oder Ubuntu 22.04+ Rechner
- Mindestens 10 GB freier Speicherplatz
- Internetverbindung
- Root-Zugang (sudo)

---

## Schritt 1: Docker installieren (falls nicht vorhanden)

Prüfe ob Docker installiert ist:

```bash
docker --version
```

Falls Docker **nicht** installiert ist:

```bash
# System aktualisieren
sudo apt-get update
sudo apt-get install -y ca-certificates curl gnupg

# Docker GPG Key hinzufügen
sudo install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/debian/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
sudo chmod a+r /etc/apt/keyrings/docker.gpg

# Docker Repository hinzufügen (Debian)
echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/debian \
  $(. /etc/os-release && echo "$VERSION_CODENAME") stable" | \
  sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

# Für Ubuntu statt Debian:
# Ersetze "debian" durch "ubuntu" in der URL oben

# Docker installieren
sudo apt-get update
sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin

# Aktuellen Benutzer zur docker-Gruppe hinzufügen (damit kein sudo nötig ist)
sudo usermod -aG docker $USER

# WICHTIG: Neu einloggen damit die Gruppenänderung wirksam wird!
# Entweder ausloggen/einloggen oder:
newgrp docker
```

Prüfe ob Docker funktioniert:

```bash
docker run --rm hello-world
```

Falls die Ausgabe "Hello from Docker!" enthält, ist alles bereit.

---

## Schritt 2: AlixOS Projekt entpacken

```bash
# ZIP entpacken (z.B. im Home-Verzeichnis)
cd ~
unzip AlixOS.zip
cd AlixOS
```

---

## Schritt 3: PiStorm32-lite FPGA Bitstream

**WICHTIG:** Du brauchst die originale PiStorm32-lite FPGA Bitstream-Datei.
Diese ist im PiStorm-Projekt enthalten.

```bash
# Bitstream besorgen (aus dem PiStorm Repo oder deiner bestehenden Emu68 SD-Karte)
# Kopiere die Datei als bitstream.bin nach:
cp /pfad/zu/bitstream.bin fpga/bitstream.bin
```

Falls du die Datei noch nicht hast:
```bash
git clone --depth 1 --branch pistorm32-lite \
  https://github.com/captain-amygdala/pistorm.git /tmp/pistorm-repo
# Die Bitstream-Datei findest du in /tmp/pistorm-repo/
# (genauer Pfad hängt vom Branch ab)
```

**Ohne Bitstream funktioniert der FPGA-Loader nicht, aber das Image wird trotzdem
gebaut. Du kannst den Bitstream auch nachträglich auf die SD-Karte kopieren nach
`/usr/local/share/alix/bitstream.bin`.**

---

## Schritt 4: AlixOS Image bauen

```bash
cd ~/AlixOS/image-build
chmod +x build-alixos.sh
./build-alixos.sh
```

Das Script:
1. Klont pi-gen (offizieller Raspberry Pi Image-Builder)
2. Konfiguriert es für CM4 64-bit
3. Installiert LXDE Desktop, WiFi, Bluetooth, Chromium
4. Kompiliert die AlixOS Kernel-Module (Tastatur, Maus, Joystick, Serial, Parallel, LED, RTC, Floppy)
5. Kompiliert den FPGA-Loader und Userspace-Tools (floppy)
6. Erzeugt das fertige SD-Karten-Image

**Dauer: ca. 20-40 Minuten** (je nach Rechner und Internetgeschwindigkeit)

Das fertige Image liegt anschließend in:
```
image-build/deploy/alixos-<datum>.img.xz
```

---

## Schritt 5: Image auf SD-Karte flashen

Auf dem Debian-Rechner:

```bash
# Image entpacken
xz -d image-build/deploy/alixos-*.img.xz

# SD-Karte finden (z.B. /dev/sdb — VORSICHT, richtiges Gerät wählen!)
lsblk

# Image flashen (ACHTUNG: alle Daten auf der SD-Karte werden gelöscht!)
sudo dd if=image-build/deploy/alixos-*.img of=/dev/sdX bs=4M status=progress
sudo sync
```

Oder auf einem anderen Rechner mit Etcher/rpi-imager:
- Kopiere die `.img` oder `.img.xz` Datei auf einen USB-Stick
- Öffne Etcher oder rpi-imager
- Wähle "Custom Image" und die AlixOS .img Datei
- Wähle die SD-Karte
- Flash

---

## Schritt 6: SD-Karte in den PiStorm32-lite einsetzen

1. SD-Karte in den CM4 Slot des PiStorm32-lite stecken
2. Amiga 1200 einschalten
3. Warten bis der LXDE Desktop erscheint (ca. 30-60 Sekunden)

---

## Nach dem ersten Boot

### Login
- **Benutzer:** alix
- **Passwort:** alix
- Auto-Login ist aktiviert — du landest direkt im Desktop

### WiFi einrichten
- Klick auf das Netzwerk-Icon im System-Tray (rechts unten)
- WiFi-Netzwerk auswählen und Passwort eingeben
- NetworkManager speichert die Verbindung automatisch

### Bluetooth
- Blueman (Bluetooth-Manager) ist im System-Tray verfügbar
- Klick auf das Bluetooth-Icon zum Koppeln von Geräten

### SSH-Zugang
- SSH ist aktiviert: `ssh alix@alixos.local` (oder IP-Adresse)

### Amiga-Input testen
```bash
# Tastatur testen
evtest /dev/input/alix-kbd

# Maus testen
evtest /dev/input/alix-mouse

# Joystick testen
jstest /dev/input/alix-joy
```

### Serielle Schnittstelle testen
```bash
# Serielle Schnittstelle ist verfügbar als /dev/ttyAMIGA0
# Beispiel: Verbindung mit 9600 Baud
minicom -D /dev/ttyAMIGA0 -b 9600

# Oder mit stty konfigurieren
stty -F /dev/ttyAMIGA0 9600 cs8 -cstopb -parenb
echo "Hello" > /dev/ttyAMIGA0
```

### Parallele Schnittstelle testen
```bash
# Parallele Schnittstelle ist verfügbar als /dev/alix-parport
# Daten senden
echo "Test" > /dev/alix-parport

# Status abfragen (programmatisch via ioctl)
```

### Power-LED steuern
```bash
# LED Status prüfen
cat /sys/class/leds/amiga::power/brightness

# LED ausschalten
echo 0 > /sys/class/leds/amiga::power/brightness

# LED einschalten
echo 1 > /sys/class/leds/amiga::power/brightness

# Heartbeat-Blinken aktivieren
echo heartbeat > /sys/class/leds/amiga::power/trigger

# Disk-Aktivitäts-Anzeige
echo disk-activity > /sys/class/leds/amiga::power/trigger
```

### CD32 Gamepad aktivieren
```bash
# Joystick-Modul mit CD32-Modus laden
sudo modprobe alix_joy cd32=1

# Gamepad testen (7 Buttons: Blue, Red, Yellow, Green, Forward, Reverse, Play)
jstest /dev/input/alix-joy

# Dauerhaft aktivieren: in /etc/modprobe.d/alix.conf eintragen:
#   options alix_joy cd32=1
```

### Echtzeituhr (RTC) nutzen
```bash
# Zeit von der Amiga-Uhr lesen
hwclock -r -f /dev/rtc0

# Systemzeit aus der Amiga-Uhr setzen (nützlich ohne WiFi/NTP)
sudo hwclock -s -f /dev/rtc0

# Aktuelle Systemzeit in die Amiga-Uhr schreiben
sudo hwclock -w -f /dev/rtc0
```

### Floppy-Disketten lesen und schreiben
```bash
# Komplette Diskette als ADF-Image sichern (mit Fortschrittsbalken)
floppy read mein_spiel.adf

# ADF-Image auf Diskette schreiben (lesbar in jedem Amiga!)
floppy write mein_spiel.adf

# Drive-Status anzeigen (Motor, Diskwechsel, Schreibschutz etc.)
floppy status

# ADF-Datei Informationen anzeigen (Filesystem-Typ, Bootblock etc.)
floppy info mein_spiel.adf

# Diskette gegen ADF-Datei verifizieren
floppy verify mein_spiel.adf

# Manuell Motor steuern
floppy motor on
floppy motor off

# Zu bestimmtem Zylinder fahren (0-79)
floppy seek 40

# Alternativ: direkt über das Device lesen
cat /dev/alix-floppy > disk.adf
dd if=/dev/alix-floppy of=disk.adf bs=5632 status=progress
```

**Hinweis zu floppy write:** Der Schreibschutz-Tab auf der Diskette muss
geschlossen sein (Loch verdeckt). Das Tool prüft automatisch ob die Diskette
schreibgeschützt ist. MFM-Precompensation wird automatisch für die inneren
Zylinder (40-79) angewendet.

---

## Fehlerbehebung

### FPGA-Loader fehlt / startet nicht
```bash
# Bitstream nachträglich kopieren
sudo cp bitstream.bin /usr/local/share/alix/bitstream.bin
# Service neu starten
sudo systemctl restart alix-fpga-load
```

### Kernel-Module nicht geladen
```bash
# Status prüfen
lsmod | grep -E "alix|pistorm"

# Manuell laden
sudo modprobe pistorm_bus
sudo modprobe alix_kbd
sudo modprobe alix_mouse
sudo modprobe alix_joy
sudo modprobe alix_serial
sudo modprobe alix_parallel
sudo modprobe alix_led
sudo modprobe alix_rtc
sudo modprobe alix_floppy
```

### Module neu kompilieren (nach Kernel-Update)
```bash
cd /opt/alix/kernel
sudo apt install linux-headers-$(uname -r)
make clean && make
sudo make install
sudo depmod -a
sudo reboot
```

### Zurück zu Emu68 wechseln
Einfach die Emu68 SD-Karte in den CM4 stecken und Amiga neu starten.
Der PiStorm wird nicht verändert — es ist alles nur Software auf der SD-Karte.

---

## Projektstruktur

```
AlixOS/
├── kernel/                    # Linux Kernel-Module
│   ├── common/                # PiStorm32-lite Bus-Treiber
│   │   ├── pistorm_bus_core.c # GPIO → Amiga-Bus Read/Write
│   │   ├── pistorm_bus_kmod.c # Modul Init + debugfs
│   │   ├── pistorm_bus.h      # API Header
│   │   └── amiga_hwreg.h      # Amiga Register-Adressen
│   ├── keyboard/alix_kbd.c    # CIA-A Tastatur-Treiber
│   ├── mouse/alix_mouse.c     # JOY0DAT Maus-Treiber
│   ├── joystick/alix_joy.c    # JOY1DAT Joystick-Treiber
│   ├── serial/alix_serial.c   # Paula Serial Port (ttyAMIGA0)
│   ├── parallel/alix_parallel.c # CIA-A PRB Parallel Port
│   ├── led/alix_led.c         # Power-LED Steuerung
│   ├── rtc/alix_rtc.c         # Echtzeituhr (RP5C01A)
│   └── floppy/alix_floppy.c   # DD-Floppy Laufwerk (ADF lesen/schreiben)
├── fpga/                      # FPGA Bitstream Loader
├── system/                    # systemd Services, udev, Xorg Config
├── distro/                    # Boot-Konfiguration, Paketlisten
├── image-build/               # pi-gen Image-Build-System
│   ├── build-alixos.sh        # Haupt-Build-Script
│   └── stage-alixos/          # Custom pi-gen Stage
├── tools/                     # Userspace-Tools
│   └── alix-floppy.c    # Floppy-Disk Utility (read/write/verify, Kommando: floppy)
├── scripts/                   # Cross-Compile und Deploy Helfer
└── BUILD-INSTRUCTIONS.md      # Diese Datei
```
