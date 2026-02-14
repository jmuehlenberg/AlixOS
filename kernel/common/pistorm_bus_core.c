// SPDX-License-Identifier: GPL-2.0
/*
 * pistorm_bus_core.c - PiStorm32-lite bus access implementation
 *
 * Provides read/write access to the Amiga 1200 address space through
 * the PiStorm32-lite FPGA by bit-banging GPIO pins on BCM2711 (CM4).
 *
 * The FPGA translates GPIO-level signals into proper 68000 bus cycles
 * on the Amiga's address/data bus.
 *
 * Based on ps_protocol.c from the PiStorm project.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include "pistorm_bus.h"
#include "amiga_hwreg.h"

/* GPIO register access (memory-mapped, volatile) */
static void __iomem *gpio_regs;
static DEFINE_SPINLOCK(bus_lock);
static unsigned long bus_lock_flags;

/* GPIO register word offsets */
#define REG_GPFSEL0     (0x00 / 4)
#define REG_GPFSEL1     (0x04 / 4)
#define REG_GPFSEL2     (0x08 / 4)
#define REG_GPSET0      (0x1C / 4)
#define REG_GPCLR0      (0x28 / 4)
#define REG_GPLEV0      (0x34 / 4)

/* Quick GPIO access macros */
static inline volatile u32 *gpio_reg(unsigned int offset)
{
	return (volatile u32 *)((u8 *)gpio_regs + offset * 4);
}

static inline void gpio_pin_output(int pin)
{
	int reg = pin / 10;
	int shift = (pin % 10) * 3;
	u32 val = readl(gpio_reg(REG_GPFSEL0 + reg));
	val &= ~(7 << shift);
	val |= (1 << shift);
	writel(val, gpio_reg(REG_GPFSEL0 + reg));
}

static inline void gpio_pin_input(int pin)
{
	int reg = pin / 10;
	int shift = (pin % 10) * 3;
	u32 val = readl(gpio_reg(REG_GPFSEL0 + reg));
	val &= ~(7 << shift);
	writel(val, gpio_reg(REG_GPFSEL0 + reg));
}

static inline void gpio_set(int pin)
{
	writel(1 << pin, gpio_reg(REG_GPSET0));
}

static inline void gpio_clr(int pin)
{
	writel(1 << pin, gpio_reg(REG_GPCLR0));
}

static inline int gpio_read(int pin)
{
	return (readl(gpio_reg(REG_GPLEV0)) >> pin) & 1;
}

static inline u32 gpio_read_all(void)
{
	return readl(gpio_reg(REG_GPLEV0));
}

static inline void gpio_write_mask(u32 set_mask, u32 clr_mask)
{
	if (clr_mask)
		writel(clr_mask, gpio_reg(REG_GPCLR0));
	if (set_mask)
		writel(set_mask, gpio_reg(REG_GPSET0));
}

/*
 * Configure GPIO pins for runtime bus operation.
 * Called after FPGA has been loaded.
 */
static void bus_setup_gpio(void)
{
	/* IPL pins as inputs (directly from Amiga) */
	gpio_pin_input(PIN_IPL0);
	gpio_pin_input(PIN_IPL1);
	gpio_pin_input(PIN_IPL2);

	/* Bus control pins */
	gpio_pin_output(PIN_TXN_RW);
	gpio_pin_output(PIN_TXN_START);
	gpio_pin_input(PIN_TXN_ACTIVE);
	gpio_pin_input(PIN_TXN_DONE);
	gpio_pin_output(PIN_TXN_SIZE0);
	gpio_pin_output(PIN_TXN_SIZE1);
	gpio_pin_output(PIN_TXN_RESET);

	/* Data/Address pins: start as outputs for address phase */
	gpio_pin_output(PIN_CDI0);
	gpio_pin_output(PIN_CDI1);
	gpio_pin_output(PIN_CDI2);
	gpio_pin_output(PIN_CDI3);
	gpio_pin_output(PIN_CDI4);
	gpio_pin_output(PIN_CDI5);
	gpio_pin_output(PIN_CDI6);
	gpio_pin_output(PIN_CDI7);
	gpio_pin_output(PIN_CBUS0);
	gpio_pin_output(PIN_CBUS1);
	gpio_pin_output(PIN_CBUS2);

	/* Default states */
	gpio_clr(PIN_TXN_START);
	gpio_set(PIN_TXN_RW);    /* Default: read mode */
	gpio_clr(PIN_TXN_RESET);
}

/*
 * Put address on the data bus pins.
 * The FPGA latches the address from the GPIO data pins during the
 * address phase of a bus transaction.
 *
 * PiStorm32-lite uses a multiplexed protocol:
 * 1. Address low byte on CDI[0:7]
 * 2. Strobe to latch
 * 3. Address high byte on CDI[0:7]
 * 4. Strobe to latch
 * 5. Address bank on CBUS[0:2]
 */
static void bus_set_addr(u32 addr)
{
	u32 set_mask = 0;
	u32 clr_mask = 0;
	u8 lo, hi, bank;

	lo = addr & 0xFF;
	hi = (addr >> 8) & 0xFF;
	bank = (addr >> 16) & 0xFF;

	/* Address low byte on CDI[0:7] */
	/* Map each bit to the corresponding GPIO pin */
	if (lo & 0x01) set_mask |= (1 << PIN_CDI0); else clr_mask |= (1 << PIN_CDI0);
	if (lo & 0x02) set_mask |= (1 << PIN_CDI1); else clr_mask |= (1 << PIN_CDI1);
	if (lo & 0x04) set_mask |= (1 << PIN_CDI2); else clr_mask |= (1 << PIN_CDI2);
	if (lo & 0x08) set_mask |= (1 << PIN_CDI3); else clr_mask |= (1 << PIN_CDI3);
	if (lo & 0x10) set_mask |= (1 << PIN_CDI4); else clr_mask |= (1 << PIN_CDI4);
	if (lo & 0x20) set_mask |= (1 << PIN_CDI5); else clr_mask |= (1 << PIN_CDI5);
	if (lo & 0x40) set_mask |= (1 << PIN_CDI6); else clr_mask |= (1 << PIN_CDI6);
	if (lo & 0x80) set_mask |= (1 << PIN_CDI7); else clr_mask |= (1 << PIN_CDI7);

	/* Also set bank bits on CBUS */
	if (bank & 0x01) set_mask |= (1 << PIN_CBUS0); else clr_mask |= (1 << PIN_CBUS0);
	if (bank & 0x02) set_mask |= (1 << PIN_CBUS1); else clr_mask |= (1 << PIN_CBUS1);
	if (bank & 0x04) set_mask |= (1 << PIN_CBUS2); else clr_mask |= (1 << PIN_CBUS2);

	gpio_write_mask(set_mask, clr_mask);

	/* Small delay for FPGA to latch */
	ndelay(100);

	/* Now put high byte - reuse CDI pins */
	set_mask = 0;
	clr_mask = 0;

	if (hi & 0x01) set_mask |= (1 << PIN_CDI0); else clr_mask |= (1 << PIN_CDI0);
	if (hi & 0x02) set_mask |= (1 << PIN_CDI1); else clr_mask |= (1 << PIN_CDI1);
	if (hi & 0x04) set_mask |= (1 << PIN_CDI2); else clr_mask |= (1 << PIN_CDI2);
	if (hi & 0x08) set_mask |= (1 << PIN_CDI3); else clr_mask |= (1 << PIN_CDI3);
	if (hi & 0x10) set_mask |= (1 << PIN_CDI4); else clr_mask |= (1 << PIN_CDI4);
	if (hi & 0x20) set_mask |= (1 << PIN_CDI5); else clr_mask |= (1 << PIN_CDI5);
	if (hi & 0x40) set_mask |= (1 << PIN_CDI6); else clr_mask |= (1 << PIN_CDI6);
	if (hi & 0x80) set_mask |= (1 << PIN_CDI7); else clr_mask |= (1 << PIN_CDI7);

	gpio_write_mask(set_mask, clr_mask);
	ndelay(100);
}

/*
 * Set the transfer size for the next bus transaction.
 */
static void bus_set_size(u8 size)
{
	if (size & 0x01)
		gpio_set(PIN_TXN_SIZE0);
	else
		gpio_clr(PIN_TXN_SIZE0);

	if (size & 0x02)
		gpio_set(PIN_TXN_SIZE1);
	else
		gpio_clr(PIN_TXN_SIZE1);
}

/*
 * Wait for the FPGA to signal transaction complete.
 * Returns 0 on success, -1 on timeout.
 */
static int bus_wait_done(void)
{
	int timeout = 10000;

	while (timeout > 0) {
		if (gpio_read(PIN_TXN_DONE))
			return 0;
		ndelay(50);
		timeout--;
	}

	pr_warn("pistorm_bus: Transaction timeout!\n");
	return -1;
}

/*
 * Read data from the GPIO data pins after a read transaction.
 * Data bus is CDI[0:7] for the low byte.
 */
static u8 bus_read_data_byte(void)
{
	u32 pins;
	u8 val = 0;

	/* Switch data pins to input mode for reading */
	gpio_pin_input(PIN_CDI0);
	gpio_pin_input(PIN_CDI1);
	gpio_pin_input(PIN_CDI2);
	gpio_pin_input(PIN_CDI3);
	gpio_pin_input(PIN_CDI4);
	gpio_pin_input(PIN_CDI5);
	gpio_pin_input(PIN_CDI6);
	gpio_pin_input(PIN_CDI7);

	ndelay(100);
	pins = gpio_read_all();

	if (pins & (1 << PIN_CDI0)) val |= 0x01;
	if (pins & (1 << PIN_CDI1)) val |= 0x02;
	if (pins & (1 << PIN_CDI2)) val |= 0x04;
	if (pins & (1 << PIN_CDI3)) val |= 0x08;
	if (pins & (1 << PIN_CDI4)) val |= 0x10;
	if (pins & (1 << PIN_CDI5)) val |= 0x20;
	if (pins & (1 << PIN_CDI6)) val |= 0x40;
	if (pins & (1 << PIN_CDI7)) val |= 0x80;

	/* Switch back to output mode */
	gpio_pin_output(PIN_CDI0);
	gpio_pin_output(PIN_CDI1);
	gpio_pin_output(PIN_CDI2);
	gpio_pin_output(PIN_CDI3);
	gpio_pin_output(PIN_CDI4);
	gpio_pin_output(PIN_CDI5);
	gpio_pin_output(PIN_CDI6);
	gpio_pin_output(PIN_CDI7);

	return val;
}

/*
 * Read a 16-bit word from the data bus.
 * Reads high byte first, then low byte (big-endian, Motorola byte order).
 */
static u16 bus_read_data_word(void)
{
	u8 hi, lo;

	/* First read gives high byte */
	hi = bus_read_data_byte();

	/* Strobe for next byte */
	gpio_set(PIN_TXN_START);
	ndelay(100);
	gpio_clr(PIN_TXN_START);

	if (bus_wait_done() < 0)
		return 0xFFFF;

	/* Second read gives low byte */
	lo = bus_read_data_byte();

	return ((u16)hi << 8) | lo;
}

/*
 * Write data byte to the GPIO data pins.
 */
static void bus_write_data_byte(u8 val)
{
	u32 set_mask = 0;
	u32 clr_mask = 0;

	if (val & 0x01) set_mask |= (1 << PIN_CDI0); else clr_mask |= (1 << PIN_CDI0);
	if (val & 0x02) set_mask |= (1 << PIN_CDI1); else clr_mask |= (1 << PIN_CDI1);
	if (val & 0x04) set_mask |= (1 << PIN_CDI2); else clr_mask |= (1 << PIN_CDI2);
	if (val & 0x08) set_mask |= (1 << PIN_CDI3); else clr_mask |= (1 << PIN_CDI3);
	if (val & 0x10) set_mask |= (1 << PIN_CDI4); else clr_mask |= (1 << PIN_CDI4);
	if (val & 0x20) set_mask |= (1 << PIN_CDI5); else clr_mask |= (1 << PIN_CDI5);
	if (val & 0x40) set_mask |= (1 << PIN_CDI6); else clr_mask |= (1 << PIN_CDI6);
	if (val & 0x80) set_mask |= (1 << PIN_CDI7); else clr_mask |= (1 << PIN_CDI7);

	gpio_write_mask(set_mask, clr_mask);
}

/* ======================================================================
 * Public API - Exported to other kernel modules
 * ====================================================================== */

int pistorm_bus_init(void)
{
	gpio_regs = ioremap(BCM2711_GPIO_BASE, BCM2711_GPIO_SIZE);
	if (!gpio_regs) {
		pr_err("pistorm_bus: Failed to ioremap GPIO registers\n");
		return -ENOMEM;
	}

	bus_setup_gpio();

	/* Initialize POTGO register for both mouse and joystick button reading */
	pistorm_write16(AMIGA_POTGO, POTGO_OUTLY | POTGO_DATLY |
				     POTGO_OUTRY | POTGO_DATRY);

	pr_info("pistorm_bus: Bus interface initialized\n");
	return 0;
}
EXPORT_SYMBOL_GPL(pistorm_bus_init);

void pistorm_bus_exit(void)
{
	if (gpio_regs) {
		iounmap(gpio_regs);
		gpio_regs = NULL;
	}
	pr_info("pistorm_bus: Bus interface shut down\n");
}
EXPORT_SYMBOL_GPL(pistorm_bus_exit);

u8 pistorm_read8(u32 addr)
{
	u8 val;

	bus_set_addr(addr);
	bus_set_size(TXN_SIZE_BYTE);
	gpio_set(PIN_TXN_RW);   /* Read mode */

	/* Start transaction */
	gpio_set(PIN_TXN_START);
	ndelay(100);
	gpio_clr(PIN_TXN_START);

	if (bus_wait_done() < 0)
		return 0xFF;

	val = bus_read_data_byte();

	return val;
}
EXPORT_SYMBOL_GPL(pistorm_read8);

u16 pistorm_read16(u32 addr)
{
	u16 val;

	bus_set_addr(addr);
	bus_set_size(TXN_SIZE_WORD);
	gpio_set(PIN_TXN_RW);   /* Read mode */

	/* Start transaction */
	gpio_set(PIN_TXN_START);
	ndelay(100);
	gpio_clr(PIN_TXN_START);

	if (bus_wait_done() < 0)
		return 0xFFFF;

	val = bus_read_data_word();

	return val;
}
EXPORT_SYMBOL_GPL(pistorm_read16);

u32 pistorm_read32(u32 addr)
{
	u32 val;
	u16 hi, lo;

	bus_set_addr(addr);
	bus_set_size(TXN_SIZE_LONG);
	gpio_set(PIN_TXN_RW);

	gpio_set(PIN_TXN_START);
	ndelay(100);
	gpio_clr(PIN_TXN_START);

	if (bus_wait_done() < 0)
		return 0xFFFFFFFF;

	hi = bus_read_data_word();

	/* Continue for low word */
	gpio_set(PIN_TXN_START);
	ndelay(100);
	gpio_clr(PIN_TXN_START);

	if (bus_wait_done() < 0)
		return 0xFFFFFFFF;

	lo = bus_read_data_word();

	val = ((u32)hi << 16) | lo;
	return val;
}
EXPORT_SYMBOL_GPL(pistorm_read32);

void pistorm_write8(u32 addr, u8 val)
{
	bus_set_addr(addr);
	bus_set_size(TXN_SIZE_BYTE);
	gpio_clr(PIN_TXN_RW);   /* Write mode */

	bus_write_data_byte(val);

	/* Start transaction */
	gpio_set(PIN_TXN_START);
	ndelay(100);
	gpio_clr(PIN_TXN_START);

	bus_wait_done();

	gpio_set(PIN_TXN_RW);   /* Back to read mode */
}
EXPORT_SYMBOL_GPL(pistorm_write8);

void pistorm_write16(u32 addr, u16 val)
{
	bus_set_addr(addr);
	bus_set_size(TXN_SIZE_WORD);
	gpio_clr(PIN_TXN_RW);

	/* Write high byte first (Motorola byte order) */
	bus_write_data_byte((val >> 8) & 0xFF);

	gpio_set(PIN_TXN_START);
	ndelay(100);
	gpio_clr(PIN_TXN_START);

	bus_wait_done();

	/* Write low byte */
	bus_write_data_byte(val & 0xFF);

	gpio_set(PIN_TXN_START);
	ndelay(100);
	gpio_clr(PIN_TXN_START);

	bus_wait_done();

	gpio_set(PIN_TXN_RW);
}
EXPORT_SYMBOL_GPL(pistorm_write16);

void pistorm_write32(u32 addr, u32 val)
{
	pistorm_write16(addr, (val >> 16) & 0xFFFF);
	pistorm_write16(addr + 2, val & 0xFFFF);
}
EXPORT_SYMBOL_GPL(pistorm_write32);

void pistorm_bus_lock(void)
{
	spin_lock_irqsave(&bus_lock, bus_lock_flags);
}
EXPORT_SYMBOL_GPL(pistorm_bus_lock);

void pistorm_bus_unlock(void)
{
	spin_unlock_irqrestore(&bus_lock, bus_lock_flags);
}
EXPORT_SYMBOL_GPL(pistorm_bus_unlock);
