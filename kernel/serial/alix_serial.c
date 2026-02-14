// SPDX-License-Identifier: GPL-2.0
/*
 * alix_serial.c - AlixOS Amiga Serial Port Driver
 *
 * Provides a standard Linux TTY device (/dev/ttyAMIGA0) by accessing the
 * Amiga 1200's built-in serial port hardware through the PiStorm32-lite
 * bus interface.
 *
 * Hardware used:
 *   - Paula SERDAT  ($DFF030): Transmit data register (write-only)
 *   - Paula SERDATR ($DFF018): Receive data + status register (read-only)
 *   - Paula SERPER  ($DFF032): Baud rate period register
 *   - Paula INTREQR ($DFF01E): Interrupt request (TBE, RBF status)
 *   - Paula INTREQ  ($DFF09C): Interrupt acknowledge
 *   - CIA-B PRA     ($BFD000): Handshaking lines (DTR, RTS, CTS, DSR, CD)
 *   - CIA-B DDRA    ($BFD200): Data direction for handshaking pins
 *
 * Serial parameters:
 *   - Baud rate formula (PAL):  divisor = (3546895 / baud) - 1
 *   - Baud rate formula (NTSC): divisor = (3579545 / baud) - 1
 *   - 8N1 default, 9-bit mode available via SERPER bit 15
 *
 * Reference: Amiga Hardware Reference Manual, Chapter 8 (Serial Port)
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/platform_device.h>
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include "pistorm_bus.h"
#include "amiga_hwreg.h"

#define DRIVER_NAME       "alix_serial"
#define DEVICE_NAME       "ttyAMIGA"
#define POLL_INTERVAL_MS  2      /* 500 Hz polling for serial */
#define TX_BUF_SIZE       4096

/* PAL clock for baud rate calculation (most Amiga 1200s are PAL) */
#define AMIGA_PAL_CLOCK   3546895
#define AMIGA_NTSC_CLOCK  3579545

static unsigned int clock_freq = AMIGA_PAL_CLOCK;
module_param(clock_freq, uint, 0444);
MODULE_PARM_DESC(clock_freq, "Amiga system clock (default: 3546895 for PAL)");

struct alix_serial {
	struct tty_port port;
	struct hrtimer timer;
	struct tty_struct *tty;

	/* TX ring buffer */
	unsigned char tx_buf[TX_BUF_SIZE];
	unsigned int tx_head;
	unsigned int tx_tail;
	spinlock_t tx_lock;

	/* Current line settings */
	speed_t baud;
	bool rts_active;
	bool dtr_active;
	bool opened;
};

static struct alix_serial *alix_ser;
static struct tty_driver *alix_tty_driver;
static struct platform_device *alix_ser_pdev;

/* ---- Hardware access helpers ------------------------------------------ */

static void alix_serial_set_baud(speed_t baud)
{
	u16 divisor;

	if (baud == 0)
		baud = 9600;

	divisor = (clock_freq / baud) - 1;
	/* Ensure 8-bit mode (bit 15 = 0) */
	divisor &= 0x7FFF;

	pistorm_write16(AMIGA_SERPER, divisor);
}

/**
 * Configure CIA-B PRA output pins for DTR and RTS.
 * CIA-B DDRA bits 6,7 must be set to output for RTS/DTR.
 */
static void alix_serial_init_handshake(void)
{
	u8 ddra;

	pistorm_bus_lock();

	/* Set DTR (bit 7) and RTS (bit 6) as outputs */
	ddra = pistorm_read8(AMIGA_CIAB_DDRA);
	ddra |= CIAB_PRA_DTR | CIAB_PRA_RTS;
	pistorm_write8(AMIGA_CIAB_DDRA, ddra);

	/* Assert DTR and RTS (active low) */
	{
		u8 pra = pistorm_read8(AMIGA_CIAB_PRA);
		pra &= ~(CIAB_PRA_DTR | CIAB_PRA_RTS);
		pistorm_write8(AMIGA_CIAB_PRA, pra);
	}

	pistorm_bus_unlock();
}

static void alix_serial_set_rts(bool active)
{
	u8 pra;

	pistorm_bus_lock();
	pra = pistorm_read8(AMIGA_CIAB_PRA);
	if (active)
		pra &= ~CIAB_PRA_RTS;  /* Active low */
	else
		pra |= CIAB_PRA_RTS;
	pistorm_write8(AMIGA_CIAB_PRA, pra);
	pistorm_bus_unlock();
}

static void alix_serial_set_dtr(bool active)
{
	u8 pra;

	pistorm_bus_lock();
	pra = pistorm_read8(AMIGA_CIAB_PRA);
	if (active)
		pra &= ~CIAB_PRA_DTR;  /* Active low */
	else
		pra |= CIAB_PRA_DTR;
	pistorm_write8(AMIGA_CIAB_PRA, pra);
	pistorm_bus_unlock();
}

static bool alix_serial_get_cts(void)
{
	u8 pra = pistorm_read8(AMIGA_CIAB_PRA);
	return !(pra & CIAB_PRA_CTS);  /* Active low */
}

static bool alix_serial_get_dsr(void)
{
	u8 pra = pistorm_read8(AMIGA_CIAB_PRA);
	return !(pra & CIAB_PRA_DSR);
}

static bool alix_serial_get_cd(void)
{
	u8 pra = pistorm_read8(AMIGA_CIAB_PRA);
	return !(pra & CIAB_PRA_CD);
}

/* ---- Polling ---------------------------------------------------------- */

static void alix_serial_rx_poll(struct alix_serial *ser)
{
	u16 serdatr;
	unsigned char ch;

	pistorm_bus_lock();
	serdatr = pistorm_read16(AMIGA_SERDATR);

	if (serdatr & SERDATR_RBF) {
		/* Data available - extract 8-bit character */
		ch = serdatr & 0xFF;

		/* Acknowledge by clearing RBF in INTREQ */
		pistorm_write16(AMIGA_INTREQ, AMIGA_INTF_RBF);
		pistorm_bus_unlock();

		/* Push to TTY layer */
		tty_insert_flip_char(&ser->port, ch,
				     (serdatr & SERDATR_OVRUN) ?
				     TTY_OVERRUN : TTY_NORMAL);
		tty_flip_buffer_push(&ser->port);
	} else {
		pistorm_bus_unlock();
	}
}

static void alix_serial_tx_poll(struct alix_serial *ser)
{
	u16 serdatr;
	unsigned char ch;
	unsigned long flags;

	/* Check if transmit buffer is empty */
	pistorm_bus_lock();
	serdatr = pistorm_read16(AMIGA_SERDATR);
	if (!(serdatr & SERDATR_TBE)) {
		pistorm_bus_unlock();
		return;
	}
	pistorm_bus_unlock();

	/* Get next byte from TX buffer */
	spin_lock_irqsave(&ser->tx_lock, flags);
	if (ser->tx_head == ser->tx_tail) {
		spin_unlock_irqrestore(&ser->tx_lock, flags);
		return;
	}
	ch = ser->tx_buf[ser->tx_tail];
	ser->tx_tail = (ser->tx_tail + 1) % TX_BUF_SIZE;
	spin_unlock_irqrestore(&ser->tx_lock, flags);

	/*
	 * Write to SERDAT: bit 8 must be set as stop bit marker.
	 * Bits 7-0 contain the data byte.
	 */
	pistorm_bus_lock();
	pistorm_write16(AMIGA_SERDAT, 0x100 | ch);
	pistorm_bus_unlock();

	/* Wake up writers if they were blocked */
	if (ser->tty)
		tty_wakeup(ser->tty);
}

static enum hrtimer_restart alix_serial_poll(struct hrtimer *timer)
{
	struct alix_serial *ser = container_of(timer, struct alix_serial,
					       timer);

	if (ser->opened) {
		alix_serial_rx_poll(ser);
		alix_serial_tx_poll(ser);
	}

	hrtimer_forward_now(timer, ms_to_ktime(POLL_INTERVAL_MS));
	return HRTIMER_RESTART;
}

/* ---- TTY operations --------------------------------------------------- */

static int alix_serial_tty_open(struct tty_struct *tty, struct file *filp)
{
	struct alix_serial *ser = alix_ser;

	tty->driver_data = ser;
	ser->tty = tty;

	/* Set default baud rate */
	ser->baud = 9600;
	alix_serial_set_baud(ser->baud);
	alix_serial_init_handshake();

	ser->rts_active = true;
	ser->dtr_active = true;
	ser->tx_head = 0;
	ser->tx_tail = 0;
	ser->opened = true;

	return tty_port_open(&ser->port, tty, filp);
}

static void alix_serial_tty_close(struct tty_struct *tty, struct file *filp)
{
	struct alix_serial *ser = tty->driver_data;

	if (ser) {
		ser->opened = false;

		/* Deassert DTR and RTS */
		alix_serial_set_dtr(false);
		alix_serial_set_rts(false);

		ser->tty = NULL;
		tty_port_close(&ser->port, tty, filp);
	}
}

static ssize_t alix_serial_tty_write(struct tty_struct *tty,
				      const u8 *buf, size_t count)
{
	struct alix_serial *ser = tty->driver_data;
	unsigned long flags;
	size_t i;
	unsigned int space;

	if (!ser)
		return -EIO;

	spin_lock_irqsave(&ser->tx_lock, flags);
	for (i = 0; i < count; i++) {
		space = (ser->tx_tail - ser->tx_head - 1 + TX_BUF_SIZE)
			% TX_BUF_SIZE;
		if (space == 0)
			break;
		ser->tx_buf[ser->tx_head] = buf[i];
		ser->tx_head = (ser->tx_head + 1) % TX_BUF_SIZE;
	}
	spin_unlock_irqrestore(&ser->tx_lock, flags);

	return i;
}

static unsigned int alix_serial_tty_write_room(struct tty_struct *tty)
{
	struct alix_serial *ser = tty->driver_data;
	unsigned long flags;
	unsigned int room;

	if (!ser)
		return 0;

	spin_lock_irqsave(&ser->tx_lock, flags);
	room = (ser->tx_tail - ser->tx_head - 1 + TX_BUF_SIZE) % TX_BUF_SIZE;
	spin_unlock_irqrestore(&ser->tx_lock, flags);

	return room;
}

static unsigned int alix_serial_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct alix_serial *ser = tty->driver_data;
	unsigned long flags;
	unsigned int count;

	if (!ser)
		return 0;

	spin_lock_irqsave(&ser->tx_lock, flags);
	count = (ser->tx_head - ser->tx_tail + TX_BUF_SIZE) % TX_BUF_SIZE;
	spin_unlock_irqrestore(&ser->tx_lock, flags);

	return count;
}

static void alix_serial_tty_set_termios(struct tty_struct *tty,
					const struct ktermios *old)
{
	struct alix_serial *ser = tty->driver_data;
	speed_t baud;

	if (!ser)
		return;

	baud = tty_get_baud_rate(tty);
	if (baud != ser->baud && baud != 0) {
		ser->baud = baud;
		alix_serial_set_baud(baud);
	}

	/* Handle DTR/RTS based on CBAUD */
	if ((tty->termios.c_cflag & CBAUD) == B0) {
		/* Hangup: drop DTR and RTS */
		alix_serial_set_dtr(false);
		alix_serial_set_rts(false);
		ser->dtr_active = false;
		ser->rts_active = false;
	} else {
		if (!ser->dtr_active) {
			alix_serial_set_dtr(true);
			ser->dtr_active = true;
		}
		if (!ser->rts_active) {
			alix_serial_set_rts(true);
			ser->rts_active = true;
		}
	}
}

static int alix_serial_tty_tiocmget(struct tty_struct *tty)
{
	struct alix_serial *ser = tty->driver_data;
	int result = 0;

	if (!ser)
		return -EIO;

	if (ser->dtr_active)
		result |= TIOCM_DTR;
	if (ser->rts_active)
		result |= TIOCM_RTS;

	pistorm_bus_lock();
	if (alix_serial_get_cts())
		result |= TIOCM_CTS;
	if (alix_serial_get_dsr())
		result |= TIOCM_DSR;
	if (alix_serial_get_cd())
		result |= TIOCM_CD;
	pistorm_bus_unlock();

	return result;
}

static int alix_serial_tty_tiocmset(struct tty_struct *tty,
				     unsigned int set, unsigned int clear)
{
	struct alix_serial *ser = tty->driver_data;

	if (!ser)
		return -EIO;

	if (set & TIOCM_DTR) {
		alix_serial_set_dtr(true);
		ser->dtr_active = true;
	}
	if (set & TIOCM_RTS) {
		alix_serial_set_rts(true);
		ser->rts_active = true;
	}

	if (clear & TIOCM_DTR) {
		alix_serial_set_dtr(false);
		ser->dtr_active = false;
	}
	if (clear & TIOCM_RTS) {
		alix_serial_set_rts(false);
		ser->rts_active = false;
	}

	return 0;
}

/* ---- TTY port operations ---------------------------------------------- */

static int alix_serial_port_activate(struct tty_port *port,
				     struct tty_struct *tty)
{
	return 0;
}

static void alix_serial_port_shutdown(struct tty_port *port)
{
}

static const struct tty_port_operations alix_serial_port_ops = {
	.activate = alix_serial_port_activate,
	.shutdown = alix_serial_port_shutdown,
};

static const struct tty_operations alix_serial_ops = {
	.open            = alix_serial_tty_open,
	.close           = alix_serial_tty_close,
	.write           = alix_serial_tty_write,
	.write_room      = alix_serial_tty_write_room,
	.chars_in_buffer = alix_serial_tty_chars_in_buffer,
	.set_termios     = alix_serial_tty_set_termios,
	.tiocmget        = alix_serial_tty_tiocmget,
	.tiocmset        = alix_serial_tty_tiocmset,
};

/* ---- Platform driver -------------------------------------------------- */

static int alix_serial_probe(struct platform_device *pdev)
{
	struct alix_serial *ser;
	struct device *tty_dev;

	ser = devm_kzalloc(&pdev->dev, sizeof(*ser), GFP_KERNEL);
	if (!ser)
		return -ENOMEM;

	spin_lock_init(&ser->tx_lock);
	tty_port_init(&ser->port);
	ser->port.ops = &alix_serial_port_ops;

	alix_ser = ser;

	/* Register TTY device */
	tty_dev = tty_port_register_device(&ser->port, alix_tty_driver, 0,
					   &pdev->dev);
	if (IS_ERR(tty_dev)) {
		tty_port_destroy(&ser->port);
		return PTR_ERR(tty_dev);
	}

	/* Start polling timer */
	hrtimer_init(&ser->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ser->timer.function = alix_serial_poll;
	hrtimer_start(&ser->timer, ms_to_ktime(POLL_INTERVAL_MS),
		      HRTIMER_MODE_REL);

	platform_set_drvdata(pdev, ser);

	dev_info(&pdev->dev,
		 "AlixOS Amiga serial port driver loaded (/dev/" DEVICE_NAME "0)\n");
	return 0;
}

static int alix_serial_remove(struct platform_device *pdev)
{
	struct alix_serial *ser = platform_get_drvdata(pdev);

	hrtimer_cancel(&ser->timer);
	tty_unregister_device(alix_tty_driver, 0);
	tty_port_destroy(&ser->port);
	alix_ser = NULL;

	dev_info(&pdev->dev, "AlixOS Amiga serial port driver unloaded\n");
	return 0;
}

static struct platform_driver alix_serial_driver = {
	.probe  = alix_serial_probe,
	.remove = alix_serial_remove,
	.driver = {
		.name = DRIVER_NAME,
	},
};

/* ---- Module init/exit ------------------------------------------------- */

static int __init alix_serial_init(void)
{
	int err;

	/* Allocate TTY driver */
	alix_tty_driver = tty_alloc_driver(1, TTY_DRIVER_REAL_RAW);
	if (IS_ERR(alix_tty_driver))
		return PTR_ERR(alix_tty_driver);

	alix_tty_driver->driver_name  = DRIVER_NAME;
	alix_tty_driver->name         = DEVICE_NAME;
	alix_tty_driver->major        = 0;  /* Dynamic major */
	alix_tty_driver->minor_start  = 0;
	alix_tty_driver->type         = TTY_DRIVER_TYPE_SERIAL;
	alix_tty_driver->subtype      = SERIAL_TYPE_NORMAL;
	alix_tty_driver->init_termios = tty_std_termios;
	alix_tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL |
						CLOCAL;

	tty_set_operations(alix_tty_driver, &alix_serial_ops);

	err = tty_register_driver(alix_tty_driver);
	if (err) {
		tty_driver_kref_put(alix_tty_driver);
		return err;
	}

	err = platform_driver_register(&alix_serial_driver);
	if (err) {
		tty_unregister_driver(alix_tty_driver);
		tty_driver_kref_put(alix_tty_driver);
		return err;
	}

	alix_ser_pdev = platform_device_register_simple(DRIVER_NAME, -1,
							NULL, 0);
	if (IS_ERR(alix_ser_pdev)) {
		platform_driver_unregister(&alix_serial_driver);
		tty_unregister_driver(alix_tty_driver);
		tty_driver_kref_put(alix_tty_driver);
		return PTR_ERR(alix_ser_pdev);
	}

	return 0;
}

static void __exit alix_serial_exit(void)
{
	platform_device_unregister(alix_ser_pdev);
	platform_driver_unregister(&alix_serial_driver);
	tty_unregister_driver(alix_tty_driver);
	tty_driver_kref_put(alix_tty_driver);
}

module_init(alix_serial_init);
module_exit(alix_serial_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AlixOS Project");
MODULE_DESCRIPTION("Amiga serial port TTY driver via PiStorm32-lite");
MODULE_ALIAS("platform:alix_serial");
