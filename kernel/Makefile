# SPDX-License-Identifier: GPL-2.0
#
# AlixOS Kernel Modules - Top-level Makefile
#
# Usage:
#   Native build (on the Pi):
#     make
#
#   Cross-compile (from dev machine):
#     make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR=/path/to/pi-kernel-headers
#
#   Install (on the Pi):
#     sudo make install

KDIR ?= /lib/modules/$(shell uname -r)/build
INSTALL_MOD_DIR ?= extra/alix

obj-m += common/
obj-m += keyboard/
obj-m += mouse/
obj-m += joystick/
obj-m += serial/
obj-m += parallel/
obj-m += led/
obj-m += rtc/
obj-m += floppy/

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) INSTALL_MOD_DIR=$(INSTALL_MOD_DIR) modules_install
	depmod -a

.PHONY: all clean install
