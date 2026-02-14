/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pistorm_bus.h - PiStorm32-lite bus access API for AlixOS
 *
 * Provides kernel-space functions to read/write Amiga hardware registers
 * through the PiStorm32-lite FPGA bus interface on Raspberry Pi CM4.
 *
 * All functions use the CM4's GPIO pins to communicate with the FPGA,
 * which translates GPIO signals into Motorola 68000 bus cycles on the
 * Amiga 1200's address/data bus.
 */

#ifndef _PISTORM_BUS_H
#define _PISTORM_BUS_H

#include <linux/types.h>

/* ========================================================================
 * BCM2711 (CM4) GPIO definitions
 * ======================================================================== */

/* BCM2711 peripheral base address (Pi 4 / CM4) */
#define BCM2711_PERI_BASE   0xFE000000
#define BCM2711_GPIO_BASE   (BCM2711_PERI_BASE + 0x200000)
#define BCM2711_GPIO_SIZE   0x1000

/* GPIO register offsets */
#define GPFSEL0             0x00  /* Function Select 0 (GPIO 0-9) */
#define GPFSEL1             0x04  /* Function Select 1 (GPIO 10-19) */
#define GPFSEL2             0x08  /* Function Select 2 (GPIO 20-29) */
#define GPSET0              0x1C  /* Pin Output Set 0 */
#define GPCLR0              0x28  /* Pin Output Clear 0 */
#define GPLEV0              0x34  /* Pin Level 0 */
#define GPPUD               0x94  /* Pull-up/down Enable (legacy) */
#define GPPUDCLK0           0x98  /* Pull-up/down Enable Clock 0 */

/* GPIO pull-up/down control (BCM2711 uses different registers) */
#define GPIO_PUP_PDN_CNTRL_REG0  0xE4
#define GPIO_PUP_PDN_CNTRL_REG1  0xE8
#define GPIO_PUP_PDN_CNTRL_REG2  0xEC
#define GPIO_PUP_PDN_CNTRL_REG3  0xF0

/* ========================================================================
 * PiStorm32-lite GPIO pin assignments
 *
 * These pins connect the CM4 to the Efinix Trion T8 FPGA on the
 * PiStorm32-lite board. The FPGA translates GPIO signals into
 * 68000 bus cycles.
 * ======================================================================== */

/* FPGA configuration pins (used during bitstream loading) */
#define PIN_CRESET1         6
#define PIN_CRESET2         7
#define PIN_CCK             22   /* Configuration clock */
#define PIN_CSS             24   /* Configuration chip select */
#define PIN_TESTN           17   /* Test pin */

/* Configuration data bus CDI[0:7] */
#define PIN_CDI0            10
#define PIN_CDI1            25
#define PIN_CDI2            9
#define PIN_CDI3            8
#define PIN_CDI4            11
#define PIN_CDI5            1
#define PIN_CDI6            16
#define PIN_CDI7            13

/* Configuration bus control */
#define PIN_CBUS0           14
#define PIN_CBUS1           15
#define PIN_CBUS2           18

/* ========================================================================
 * PiStorm32-lite Bus Protocol Control/Status pins
 *
 * After FPGA is configured, these pins serve as the runtime bus
 * interface between CM4 and Amiga hardware.
 * ======================================================================== */

/* IPL (Interrupt Priority Level) pins - directly from Amiga */
#define PIN_IPL0            3
#define PIN_IPL1            2
#define PIN_IPL2            0

/* Bus control pins */
#define PIN_TXN_RW          5    /* Transaction read/write: 1=read, 0=write */
#define PIN_TXN_START       4    /* Transaction start strobe */
#define PIN_TXN_ACTIVE      19   /* Transaction in progress (from FPGA) */
#define PIN_TXN_DONE        20   /* Transaction complete (from FPGA) */
#define PIN_TXN_SIZE0       21   /* Transfer size bit 0 */
#define PIN_TXN_SIZE1       26   /* Transfer size bit 1 */
#define PIN_TXN_RESET       27   /* Bus reset */

/* Data/Address multiplexed bus (active after FPGA config) */
/* Reuses CDI/CBUS pins for data transfer */

/* ========================================================================
 * Bus transaction types and sizes
 * ======================================================================== */

#define TXN_SIZE_BYTE       0x00
#define TXN_SIZE_WORD       0x01
#define TXN_SIZE_LONG       0x02

/* ========================================================================
 * Public API - Exported for use by input driver modules
 * ======================================================================== */

/**
 * pistorm_bus_init() - Initialize PiStorm32-lite bus interface
 *
 * Maps GPIO registers, configures pin directions, and prepares
 * the bus for read/write operations. Must be called before any
 * other bus functions.
 *
 * Return: 0 on success, negative errno on failure
 */
int pistorm_bus_init(void);

/**
 * pistorm_bus_exit() - Shutdown bus interface
 *
 * Unmaps GPIO registers and releases resources.
 */
void pistorm_bus_exit(void);

/**
 * pistorm_read8() - Read a byte from Amiga address space
 * @addr: 24-bit Amiga address (e.g., 0xBFE001 for CIA-A PRA)
 *
 * Performs a single 68000 byte read cycle through the FPGA.
 *
 * Return: 8-bit value read from the address
 */
u8 pistorm_read8(u32 addr);

/**
 * pistorm_read16() - Read a word from Amiga address space
 * @addr: 24-bit Amiga address (must be word-aligned)
 *
 * Return: 16-bit value read from the address
 */
u16 pistorm_read16(u32 addr);

/**
 * pistorm_read32() - Read a longword from Amiga address space
 * @addr: 24-bit Amiga address (must be longword-aligned)
 *
 * Return: 32-bit value read from the address
 */
u32 pistorm_read32(u32 addr);

/**
 * pistorm_write8() - Write a byte to Amiga address space
 * @addr: 24-bit Amiga address
 * @val: 8-bit value to write
 */
void pistorm_write8(u32 addr, u8 val);

/**
 * pistorm_write16() - Write a word to Amiga address space
 * @addr: 24-bit Amiga address (must be word-aligned)
 * @val: 16-bit value to write
 */
void pistorm_write16(u32 addr, u16 val);

/**
 * pistorm_write32() - Write a longword to Amiga address space
 * @addr: 24-bit Amiga address (must be longword-aligned)
 * @val: 32-bit value to write
 */
void pistorm_write32(u32 addr, u32 val);

/**
 * pistorm_bus_lock() - Acquire exclusive bus access
 *
 * Acquires a spinlock with interrupts disabled. Use when performing
 * multi-register operations that must be atomic (e.g., keyboard
 * handshake: read SDR, toggle CRA, wait, toggle CRA).
 *
 * Must be paired with pistorm_bus_unlock().
 */
void pistorm_bus_lock(void);

/**
 * pistorm_bus_unlock() - Release exclusive bus access
 */
void pistorm_bus_unlock(void);

#endif /* _PISTORM_BUS_H */
