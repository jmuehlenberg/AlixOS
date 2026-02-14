# SPDX-License-Identifier: GPL-2.0
#
# AlixOS Userspace Tools
#
# Build natively on the Pi:
#   make
#
# Cross-compile:
#   make CC=aarch64-linux-gnu-gcc

CC ?= gcc
CFLAGS = -Wall -Wextra -O2
TARGETS = floppy

all: $(TARGETS)

floppy: alix-floppy.c
	$(CC) $(CFLAGS) -o $@ $<

install: $(TARGETS)
	install -m 755 $(TARGETS) /usr/local/bin/

clean:
	rm -f $(TARGETS)

.PHONY: all install clean
