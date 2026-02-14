// SPDX-License-Identifier: GPL-2.0
/*
 * alix_kbd.c - AlixOS Amiga Keyboard Input Driver
 *
 * Reads keyboard data from the Amiga 1200's CIA-A serial data register
 * via the PiStorm32-lite bus interface and presents it as a standard
 * Linux input device.
 *
 * The Amiga keyboard sends scancodes serially through CIA-A's SP pin.
 * When a complete byte is received, the CIA-A ICR SP bit is set.
 * We poll for this event and perform the required handshake protocol.
 *
 * Keyboard protocol:
 *   1. CIA-A ICR bit 3 (SP) signals byte ready
 *   2. Read SDR register, invert bits: raw = ~SDR
 *   3. Handshake: set CRA bit 6 (SP=output), wait 85us, clear CRA bit 6
 *   4. Scancode = raw >> 1, key-up = raw & 1
 *
 * Scancode table derived from Linux kernel drivers/input/keyboard/amikbd.c
 *
 * Reference: Amiga Hardware Reference Manual, Chapter 9 (Keyboard)
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include "pistorm_bus.h"
#include "amiga_hwreg.h"

#define DRIVER_NAME       "alix_kbd"
#define POLL_INTERVAL_MS  5   /* 200 Hz polling */

/*
 * Amiga raw scancode to Linux keycode mapping table.
 * Index = Amiga scancode (0x00 - 0x77)
 * Value = Linux KEY_* constant
 *
 * Directly taken from Linux kernel's amikbd.c
 */
static const unsigned short amiga_keycode[0x78] = {
	/* 0x00 */ KEY_GRAVE,      KEY_1,          KEY_2,          KEY_3,
	/* 0x04 */ KEY_4,          KEY_5,          KEY_6,          KEY_7,
	/* 0x08 */ KEY_8,          KEY_9,          KEY_0,          KEY_MINUS,
	/* 0x0C */ KEY_EQUAL,      KEY_BACKSLASH,  KEY_RESERVED,   KEY_KP0,

	/* 0x10 */ KEY_Q,          KEY_W,          KEY_E,          KEY_R,
	/* 0x14 */ KEY_T,          KEY_Y,          KEY_U,          KEY_I,
	/* 0x18 */ KEY_O,          KEY_P,          KEY_LEFTBRACE,  KEY_RIGHTBRACE,
	/* 0x1C */ KEY_RESERVED,   KEY_KP1,        KEY_KP2,        KEY_KP3,

	/* 0x20 */ KEY_A,          KEY_S,          KEY_D,          KEY_F,
	/* 0x24 */ KEY_G,          KEY_H,          KEY_J,          KEY_K,
	/* 0x28 */ KEY_L,          KEY_SEMICOLON,  KEY_APOSTROPHE, KEY_BACKSLASH,
	/* 0x2C */ KEY_RESERVED,   KEY_KP4,        KEY_KP5,        KEY_KP6,

	/* 0x30 */ KEY_102ND,      KEY_Z,          KEY_X,          KEY_C,
	/* 0x34 */ KEY_V,          KEY_B,          KEY_N,          KEY_M,
	/* 0x38 */ KEY_COMMA,      KEY_DOT,        KEY_SLASH,      KEY_RESERVED,
	/* 0x3C */ KEY_KPDOT,      KEY_KP7,        KEY_KP8,        KEY_KP9,

	/* 0x40 */ KEY_SPACE,      KEY_BACKSPACE,  KEY_TAB,        KEY_KPENTER,
	/* 0x44 */ KEY_ENTER,      KEY_ESC,        KEY_DELETE,     KEY_RESERVED,
	/* 0x48 */ KEY_RESERVED,   KEY_RESERVED,   KEY_KPMINUS,    KEY_RESERVED,
	/* 0x4C */ KEY_UP,         KEY_DOWN,       KEY_RIGHT,      KEY_LEFT,

	/* 0x50 */ KEY_F1,         KEY_F2,         KEY_F3,         KEY_F4,
	/* 0x54 */ KEY_F5,         KEY_F6,         KEY_F7,         KEY_F8,
	/* 0x58 */ KEY_F9,         KEY_F10,        KEY_KPLEFTPAREN, KEY_KPRIGHTPAREN,
	/* 0x5C */ KEY_KPSLASH,    KEY_KPASTERISK, KEY_KPPLUS,     KEY_HELP,

	/* 0x60 */ KEY_LEFTSHIFT,  KEY_RIGHTSHIFT, KEY_CAPSLOCK,   KEY_LEFTCTRL,
	/* 0x64 */ KEY_LEFTALT,    KEY_RIGHTALT,   KEY_LEFTMETA,   KEY_RIGHTMETA,

	/* 0x68-0x77: reserved/unused on standard Amiga keyboards */
	[0x68 ... 0x77] = KEY_RESERVED,
};

struct alix_kbd {
	struct input_dev *idev;
	struct hrtimer timer;
};

static enum hrtimer_restart alix_kbd_poll(struct hrtimer *timer)
{
	struct alix_kbd *kbd = container_of(timer, struct alix_kbd, timer);
	u8 icr, sdr, raw, scancode;
	int key_up;

	pistorm_bus_lock();

	/*
	 * Read CIA-A ICR to check if a keyboard byte is ready.
	 * Note: reading ICR clears all interrupt flags, so we must
	 * handle the data immediately.
	 */
	icr = pistorm_read8(AMIGA_CIAA_ICR);

	if (icr & CIAA_ICR_SP) {
		/* Keyboard byte ready - read serial data register */
		sdr = pistorm_read8(AMIGA_CIAA_SDR);

		/*
		 * Invert the data (Amiga keyboard sends inverted).
		 * Bit 0 = key state: 0 = pressed, 1 = released
		 * Bits 7-1 = scancode
		 */
		raw = ~sdr;
		key_up = raw & 1;
		scancode = raw >> 1;

		/*
		 * Handshake protocol:
		 * 1. Set CRA bit 6 to switch SP pin to output mode
		 * 2. Wait at least 85 microseconds
		 * 3. Clear CRA bit 6 to switch back to input mode
		 * This acknowledges the byte to the keyboard controller.
		 */
		{
			u8 cra = pistorm_read8(AMIGA_CIAA_CRA);
			pistorm_write8(AMIGA_CIAA_CRA, cra | CIAA_CRA_SPMODE);
			udelay(85);
			pistorm_write8(AMIGA_CIAA_CRA, cra & ~CIAA_CRA_SPMODE);
		}

		pistorm_bus_unlock();

		/* Report key event if scancode is valid */
		if (scancode < ARRAY_SIZE(amiga_keycode) &&
		    amiga_keycode[scancode] != KEY_RESERVED) {
			input_report_key(kbd->idev, amiga_keycode[scancode],
					 !key_up);
			input_sync(kbd->idev);
		}
	} else {
		pistorm_bus_unlock();
	}

	hrtimer_forward_now(timer, ms_to_ktime(POLL_INTERVAL_MS));
	return HRTIMER_RESTART;
}

static int alix_kbd_probe(struct platform_device *pdev)
{
	struct alix_kbd *kbd;
	struct input_dev *idev;
	int i, err;

	kbd = devm_kzalloc(&pdev->dev, sizeof(*kbd), GFP_KERNEL);
	if (!kbd)
		return -ENOMEM;

	idev = devm_input_allocate_device(&pdev->dev);
	if (!idev)
		return -ENOMEM;

	kbd->idev = idev;

	idev->name = "AlixOS Amiga Keyboard";
	idev->phys = "alix/input0";
	idev->id.bustype = BUS_HOST;
	idev->id.vendor  = 0x0001;
	idev->id.product = 0x0001;
	idev->id.version = 0x0100;

	/* Set up keyboard capabilities */
	__set_bit(EV_KEY, idev->evbit);
	__set_bit(EV_REP, idev->evbit);  /* Enable key repeat */

	for (i = 0; i < (int)ARRAY_SIZE(amiga_keycode); i++) {
		if (amiga_keycode[i] != KEY_RESERVED)
			__set_bit(amiga_keycode[i], idev->keybit);
	}

	err = input_register_device(idev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register input device\n");
		return err;
	}

	/* Start polling timer */
	hrtimer_init(&kbd->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	kbd->timer.function = alix_kbd_poll;
	hrtimer_start(&kbd->timer, ms_to_ktime(POLL_INTERVAL_MS),
		      HRTIMER_MODE_REL);

	platform_set_drvdata(pdev, kbd);

	dev_info(&pdev->dev, "AlixOS Amiga keyboard driver loaded\n");
	return 0;
}

static int alix_kbd_remove(struct platform_device *pdev)
{
	struct alix_kbd *kbd = platform_get_drvdata(pdev);

	hrtimer_cancel(&kbd->timer);

	dev_info(&pdev->dev, "AlixOS Amiga keyboard driver unloaded\n");
	return 0;
}

/* ---- Module plumbing ------------------------------------------------- */

static struct platform_device *alix_kbd_pdev;

static struct platform_driver alix_kbd_driver = {
	.probe  = alix_kbd_probe,
	.remove = alix_kbd_remove,
	.driver = {
		.name = DRIVER_NAME,
	},
};

static int __init alix_kbd_init(void)
{
	int err;

	err = platform_driver_register(&alix_kbd_driver);
	if (err)
		return err;

	alix_kbd_pdev = platform_device_register_simple(DRIVER_NAME, -1,
							NULL, 0);
	if (IS_ERR(alix_kbd_pdev)) {
		platform_driver_unregister(&alix_kbd_driver);
		return PTR_ERR(alix_kbd_pdev);
	}

	return 0;
}

static void __exit alix_kbd_exit(void)
{
	platform_device_unregister(alix_kbd_pdev);
	platform_driver_unregister(&alix_kbd_driver);
}

module_init(alix_kbd_init);
module_exit(alix_kbd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AlixOS Project");
MODULE_DESCRIPTION("Amiga keyboard input driver via PiStorm32-lite");
MODULE_ALIAS("platform:alix_kbd");
