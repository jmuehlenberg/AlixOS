// SPDX-License-Identifier: GPL-2.0
/*
 * alix_joy.c - AlixOS Amiga Joystick / CD32 Gamepad Input Driver
 *
 * Reads digital joystick directions from JOY1DAT and fire buttons
 * from CIA-A PRA and POTGOR via PiStorm32-lite bus.
 *
 * Optionally supports the CD32 gamepad protocol (module parameter cd32=1).
 * The CD32 gamepad has 7 buttons read via a shift register:
 *   - Pin 5 (POTGO OUTRY/DATRY): Directly active/Load (active low)
 *   - Pin 9 (POTGO OUTRX/DATRX): Clock
 *   - Pin 6 (CIA-A PRA bit 7):  Serial data output
 *
 * Button order from shift register (active low):
 *   Bit 0: Blue    (Fire 3)
 *   Bit 1: Red     (Fire 1)
 *   Bit 2: Yellow  (Fire 2)
 *   Bit 3: Green
 *   Bit 4: Forward (Right Shoulder)
 *   Bit 5: Reverse (Left Shoulder)
 *   Bit 6: Play    (Start/Pause)
 *
 * Joystick direction decoding (from Linux kernel amijoy.c):
 *   Horizontal: axis_x = ((dat >> 1) & 1) - ((dat >> 9) & 1)
 *   Vertical:   data_y = ~(dat ^ (dat << 1))
 *               axis_y = ((data_y >> 1) & 1) - ((data_y >> 9) & 1)
 *
 * Reference: Amiga Hardware Reference Manual, Chapter 8 (Joystick Port)
 *            CD32 Developer Documentation (Shift Register Protocol)
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include "pistorm_bus.h"
#include "amiga_hwreg.h"

#define DRIVER_NAME       "alix_joy"
#define POLL_INTERVAL_MS  10  /* 100 Hz, sufficient for joystick */

/* CD32 shift register timing */
#define CD32_CLOCK_DELAY_US  10  /* Microseconds per clock half-cycle */

static bool cd32;
module_param(cd32, bool, 0444);
MODULE_PARM_DESC(cd32, "Enable CD32 gamepad mode (7 buttons via shift register)");

struct alix_joy {
	struct input_dev *idev;
	struct hrtimer timer;
};

/*
 * Read CD32 gamepad buttons via the shift register protocol.
 *
 * The CD32 controller contains a 74LS165-compatible shift register.
 * When pin 5 is driven low, the current button states are parallel-loaded.
 * Then each clock pulse on pin 9 shifts the next bit out on pin 6.
 *
 * Must be called with bus lock held.
 * Returns 7-bit button state (active high — already inverted).
 */
static u8 cd32_read_buttons(void)
{
	u16 potgo;
	u8 buttons = 0;
	u8 pra;
	int i;

	/*
	 * Save current POTGO state and enter shift register mode.
	 *
	 * Step 1: Drive pin 5 LOW (OUTRY=1, DATRY=0) → parallel load.
	 * Step 2: Drive pin 9 as output, start LOW (OUTRX=1, DATRX=0).
	 */
	potgo = pistorm_read16(AMIGA_POTGOR);

	/* Enable output on both pot pins for port 2 */
	potgo |= POTGO_OUTRY | POTGO_OUTRX;
	/* Drive both LOW: clear data bits */
	potgo &= ~(POTGO_DATRY | POTGO_DATRX);

	pistorm_write16(AMIGA_POTGO, potgo);
	udelay(CD32_CLOCK_DELAY_US);

	/*
	 * Read 7 buttons. After parallel load, the first bit (Blue)
	 * is immediately available on pin 6 (fire button / CIA-A PRA bit 7).
	 * Each subsequent clock pulse shifts the next bit out.
	 */
	for (i = 0; i < 7; i++) {
		/* Read data bit from fire button pin (active low) */
		pra = pistorm_read8(AMIGA_CIAA_PRA);
		if (!(pra & CIAA_PRA_FIR1))
			buttons |= (1 << i);

		/* Clock pulse: HIGH then LOW */
		potgo |= POTGO_DATRX;
		pistorm_write16(AMIGA_POTGO, potgo);
		udelay(CD32_CLOCK_DELAY_US);

		potgo &= ~POTGO_DATRX;
		pistorm_write16(AMIGA_POTGO, potgo);
		udelay(CD32_CLOCK_DELAY_US);
	}

	/*
	 * Exit shift register mode:
	 * Release pin 5 (OUTRY=0) and pin 9 (OUTRX=0),
	 * set data bits high (inactive).
	 */
	potgo &= ~(POTGO_OUTRY | POTGO_OUTRX);
	potgo |= POTGO_DATRY | POTGO_DATRX;
	pistorm_write16(AMIGA_POTGO, potgo);

	return buttons;
}

static enum hrtimer_restart alix_joy_poll(struct hrtimer *timer)
{
	struct alix_joy *joy = container_of(timer, struct alix_joy, timer);
	u16 dat, potgor, data_y;
	u8 pra;
	int axis_x, axis_y;

	pistorm_bus_lock();

	/* Read joystick port 2 data (directions) */
	dat = pistorm_read16(AMIGA_JOY1DAT);

	if (cd32) {
		/*
		 * CD32 mode: read all 7 buttons via shift register.
		 * Direction reading from JOY1DAT is unaffected.
		 * The shift register temporarily takes over the fire button
		 * pin, so we read buttons via the shift register instead.
		 */
		u8 buttons = cd32_read_buttons();

		pistorm_bus_unlock();

		/* Decode directions (same as standard mode) */
		axis_x = (int)((dat >> 1) & 1) - (int)((dat >> 9) & 1);
		data_y = ~(dat ^ (dat << 1));
		axis_y = (int)((data_y >> 1) & 1) - (int)((data_y >> 9) & 1);

		input_report_abs(joy->idev, ABS_X, axis_x);
		input_report_abs(joy->idev, ABS_Y, axis_y);

		/* CD32 buttons (active high after inversion in cd32_read_buttons) */
		input_report_key(joy->idev, BTN_B,     !!(buttons & 0x01)); /* Blue */
		input_report_key(joy->idev, BTN_A,     !!(buttons & 0x02)); /* Red */
		input_report_key(joy->idev, BTN_Y,     !!(buttons & 0x04)); /* Yellow */
		input_report_key(joy->idev, BTN_X,     !!(buttons & 0x08)); /* Green */
		input_report_key(joy->idev, BTN_TR,    !!(buttons & 0x10)); /* Forward / R Shoulder */
		input_report_key(joy->idev, BTN_TL,    !!(buttons & 0x20)); /* Reverse / L Shoulder */
		input_report_key(joy->idev, BTN_START, !!(buttons & 0x40)); /* Play */
	} else {
		/* Standard 2-button joystick mode */
		int fire1, fire2;

		pra = pistorm_read8(AMIGA_CIAA_PRA);
		potgor = pistorm_read16(AMIGA_POTGOR);

		pistorm_bus_unlock();

		axis_x = (int)((dat >> 1) & 1) - (int)((dat >> 9) & 1);
		data_y = ~(dat ^ (dat << 1));
		axis_y = (int)((data_y >> 1) & 1) - (int)((data_y >> 9) & 1);

		fire1 = !(pra & CIAA_PRA_FIR1);
		fire2 = !(potgor & POTGO_DATRY);

		input_report_abs(joy->idev, ABS_X, axis_x);
		input_report_abs(joy->idev, ABS_Y, axis_y);

		input_report_key(joy->idev, BTN_TRIGGER, fire1);
		input_report_key(joy->idev, BTN_THUMB, fire2);
	}

	input_sync(joy->idev);

	hrtimer_forward_now(timer, ms_to_ktime(POLL_INTERVAL_MS));
	return HRTIMER_RESTART;
}

static int alix_joy_probe(struct platform_device *pdev)
{
	struct alix_joy *joy;
	struct input_dev *idev;
	int err;

	joy = devm_kzalloc(&pdev->dev, sizeof(*joy), GFP_KERNEL);
	if (!joy)
		return -ENOMEM;

	idev = devm_input_allocate_device(&pdev->dev);
	if (!idev)
		return -ENOMEM;

	joy->idev = idev;

	if (cd32) {
		idev->name = "AlixOS Amiga CD32 Gamepad";
		idev->id.product = 0x0032;  /* CD32 identifier */
	} else {
		idev->name = "AlixOS Amiga Joystick";
		idev->id.product = 0x0003;
	}

	idev->phys = "alix/input2";
	idev->id.bustype = BUS_HOST;
	idev->id.vendor  = 0x0001;
	idev->id.version = 0x0100;

	/* Absolute axes: digital joystick reports -1, 0, or +1 */
	__set_bit(EV_ABS, idev->evbit);
	input_set_abs_params(idev, ABS_X, -1, 1, 0, 0);
	input_set_abs_params(idev, ABS_Y, -1, 1, 0, 0);

	/* Buttons */
	__set_bit(EV_KEY, idev->evbit);

	if (cd32) {
		/* CD32 gamepad: 7 buttons */
		__set_bit(BTN_A, idev->keybit);      /* Red */
		__set_bit(BTN_B, idev->keybit);      /* Blue */
		__set_bit(BTN_X, idev->keybit);      /* Green */
		__set_bit(BTN_Y, idev->keybit);      /* Yellow */
		__set_bit(BTN_TL, idev->keybit);     /* Reverse / Left Shoulder */
		__set_bit(BTN_TR, idev->keybit);     /* Forward / Right Shoulder */
		__set_bit(BTN_START, idev->keybit);  /* Play */
	} else {
		/* Standard joystick: 2 buttons */
		__set_bit(BTN_TRIGGER, idev->keybit);
		__set_bit(BTN_THUMB, idev->keybit);
	}

	err = input_register_device(idev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register input device\n");
		return err;
	}

	/* Start polling timer */
	hrtimer_init(&joy->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	joy->timer.function = alix_joy_poll;
	hrtimer_start(&joy->timer, ms_to_ktime(POLL_INTERVAL_MS),
		      HRTIMER_MODE_REL);

	platform_set_drvdata(pdev, joy);

	dev_info(&pdev->dev, "AlixOS Amiga %s driver loaded\n",
		 cd32 ? "CD32 gamepad" : "joystick");
	return 0;
}

static int alix_joy_remove(struct platform_device *pdev)
{
	struct alix_joy *joy = platform_get_drvdata(pdev);

	hrtimer_cancel(&joy->timer);

	dev_info(&pdev->dev, "AlixOS Amiga joystick driver unloaded\n");
	return 0;
}

/* ---- Module plumbing ------------------------------------------------- */

static struct platform_device *alix_joy_pdev;

static struct platform_driver alix_joy_driver = {
	.probe  = alix_joy_probe,
	.remove = alix_joy_remove,
	.driver = {
		.name = DRIVER_NAME,
	},
};

static int __init alix_joy_init(void)
{
	int err;

	err = platform_driver_register(&alix_joy_driver);
	if (err)
		return err;

	alix_joy_pdev = platform_device_register_simple(DRIVER_NAME, -1,
							NULL, 0);
	if (IS_ERR(alix_joy_pdev)) {
		platform_driver_unregister(&alix_joy_driver);
		return PTR_ERR(alix_joy_pdev);
	}

	return 0;
}

static void __exit alix_joy_exit(void)
{
	platform_device_unregister(alix_joy_pdev);
	platform_driver_unregister(&alix_joy_driver);
}

module_init(alix_joy_init);
module_exit(alix_joy_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AlixOS Project");
MODULE_DESCRIPTION("Amiga joystick/CD32 gamepad input driver via PiStorm32-lite");
MODULE_ALIAS("platform:alix_joy");
