# SPDX-License-Identifier: GPL-2.0
#
# AlixOS FPGA Bitstream Loader
#
# Build natively on the Pi:
#   make
#
# Cross-compile:
#   make CC=aarch64-linux-gnu-gcc

CC ?= gcc
CFLAGS = -Wall -Wextra -O2
TARGET = alix-fpga-load

all: $(TARGET)

$(TARGET): alix-fpga-load.c
	$(CC) $(CFLAGS) -o $@ $<

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/
	mkdir -p /usr/local/share/alix
	@echo "Place your PiStorm32-lite bitstream.bin in /usr/local/share/alix/"

clean:
	rm -f $(TARGET)

.PHONY: all install clean
