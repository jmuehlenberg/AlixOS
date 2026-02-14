// SPDX-License-Identifier: GPL-2.0
/*
 * alix_mouse.c - AlixOS Amiga Mouse Input Driver
 *
 * Reads mouse movement from the Amiga 1200's JOY0DAT register and
 * button state from CIA-A PRA and POTGOR via PiStorm32-lite bus.
 *
 * The Amiga uses quadrature encoding for mouse movement. The JOY0DAT
 * register contains two 8-bit counters:
 *   - Bits 7-0:  X counter (horizontal)
 *   - Bits 15-8: Y counter (vertical)
 *
 * Movement is detected by comparing current counter values to previous
 * values and computing signed deltas (handling 8-bit wrap-around).
 *
 * Buttons:
 *   - LMB: CIA-A PRA bit 6 (active low)
 *   - RMB: POTGOR bit 11 (DATLY, active low)
 *   - MMB: POTGOR bit 9 (DATLX, active low) [if 3-button mouse]
 *
 * Reference: Amiga Hardware Reference Manual, Appendix F (Mouse)
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/hrtimer.h>
#include "pistorm_bus.h"
#include "amiga_hwreg.h"

#define DRIVER_NAME       "alix_mouse"
#define POLL_INTERVAL_MS  5   /* 200 Hz polling */

static int sensitivity = 1;
module_param(sensitivity, int, 0644);
MODULE_PARM_DESC(sensitivity, "Mouse sensitivity multiplier (default 1)");

struct alix_mouse {
	struct input_dev *idev;
	struct hrtimer timer;
	u8 last_x;
	u8 last_y;
};

static enum hrtimer_restart alix_mouse_poll(struct hrtimer *timer)
{
	struct alix_mouse *mouse = container_of(timer, struct alix_mouse, timer);
	u16 joydat;
	u8 x_raw, y_raw, pra;
	u16 potgor;
	int dx, dy;
	int lmb, rmb, mmb;

	pistorm_bus_lock();

	/* Read JOY0DAT - mouse port 1 counter */
	joydat = pistorm_read16(AMIGA_JOY0DAT);

	/* Read button registers */
	pra = pistorm_read8(AMIGA_CIAA_PRA);
	potgor = pistorm_read16(AMIGA_POTGOR);

	pistorm_bus_unlock();

	/* Extract X and Y counters */
	x_raw = joydat & 0xFF;
	y_raw = (joydat >> 8) & 0xFF;

	/*
	 * Compute signed deltas with 8-bit wrap-around handling.
	 * The counters are 8-bit and wrap from 255 to 0 (or 0 to 255).
	 * By computing the difference modulo 256 and converting to signed,
	 * we correctly handle movement in either direction.
	 */
	dx = (int)((x_raw - mouse->last_x) & 0xFF);
	if (dx > 127)
		dx -= 256;

	dy = (int)((y_raw - mouse->last_y) & 0xFF);
	if (dy > 127)
		dy -= 256;

	/* Apply sensitivity multiplier */
	dx *= sensitivity;
	dy *= sensitivity;

	/* Update stored counter values */
	mouse->last_x = x_raw;
	mouse->last_y = y_raw;

	/* Decode buttons (active low) */
	lmb = !(pra & CIAA_PRA_FIR0);         /* CIA-A PRA bit 6 */
	rmb = !(potgor & POTGO_DATLY);         /* POTGOR bit 11 */
	mmb = !(potgor & POTGO_DATLX);         /* POTGOR bit 9 */

	/* Report movement (only if non-zero to avoid unnecessary events) */
	if (dx != 0)
		input_report_rel(mouse->idev, REL_X, dx);
	if (dy != 0)
		input_report_rel(mouse->idev, REL_Y, dy);

	/* Report buttons */
	input_report_key(mouse->idev, BTN_LEFT, lmb);
	input_report_key(mouse->idev, BTN_RIGHT, rmb);
	input_report_key(mouse->idev, BTN_MIDDLE, mmb);

	input_sync(mouse->idev);

	hrtimer_forward_now(timer, ms_to_ktime(POLL_INTERVAL_MS));
	return HRTIMER_RESTART;
}

static int alix_mouse_probe(struct platform_device *pdev)
{
	struct alix_mouse *mouse;
	struct input_dev *idev;
	int err;

	mouse = devm_kzalloc(&pdev->dev, sizeof(*mouse), GFP_KERNEL);
	if (!mouse)
		return -ENOMEM;

	idev = devm_input_allocate_device(&pdev->dev);
	if (!idev)
		return -ENOMEM;

	mouse->idev = idev;

	idev->name = "AlixOS Amiga Mouse";
	idev->phys = "alix/input1";
	idev->id.bustype = BUS_HOST;
	idev->id.vendor  = 0x0001;
	idev->id.product = 0x0002;
	idev->id.version = 0x0100;

	/* Relative axes */
	__set_bit(EV_REL, idev->evbit);
	__set_bit(REL_X, idev->relbit);
	__set_bit(REL_Y, idev->relbit);

	/* Mouse buttons */
	__set_bit(EV_KEY, idev->evbit);
	__set_bit(BTN_LEFT, idev->keybit);
	__set_bit(BTN_RIGHT, idev->keybit);
	__set_bit(BTN_MIDDLE, idev->keybit);

	err = input_register_device(idev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register input device\n");
		return err;
	}

	/*
	 * Initialize last counter values by reading current state.
	 * This prevents a large initial delta on first poll.
	 */
	{
		u16 joydat;

		pistorm_bus_lock();
		joydat = pistorm_read16(AMIGA_JOY0DAT);
		pistorm_bus_unlock();

		mouse->last_x = joydat & 0xFF;
		mouse->last_y = (joydat >> 8) & 0xFF;
	}

	/* Start polling timer */
	hrtimer_init(&mouse->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	mouse->timer.function = alix_mouse_poll;
	hrtimer_start(&mouse->timer, ms_to_ktime(POLL_INTERVAL_MS),
		      HRTIMER_MODE_REL);

	platform_set_drvdata(pdev, mouse);

	dev_info(&pdev->dev, "AlixOS Amiga mouse driver loaded\n");
	return 0;
}

static int alix_mouse_remove(struct platform_device *pdev)
{
	struct alix_mouse *mouse = platform_get_drvdata(pdev);

	hrtimer_cancel(&mouse->timer);

	dev_info(&pdev->dev, "AlixOS Amiga mouse driver unloaded\n");
	return 0;
}

/* ---- Module plumbing ------------------------------------------------- */

static struct platform_device *alix_mouse_pdev;

static struct platform_driver alix_mouse_driver = {
	.probe  = alix_mouse_probe,
	.remove = alix_mouse_remove,
	.driver = {
		.name = DRIVER_NAME,
	},
};

static int __init alix_mouse_init(void)
{
	int err;

	err = platform_driver_register(&alix_mouse_driver);
	if (err)
		return err;

	alix_mouse_pdev = platform_device_register_simple(DRIVER_NAME, -1,
							  NULL, 0);
	if (IS_ERR(alix_mouse_pdev)) {
		platform_driver_unregister(&alix_mouse_driver);
		return PTR_ERR(alix_mouse_pdev);
	}

	return 0;
}

static void __exit alix_mouse_exit(void)
{
	platform_device_unregister(alix_mouse_pdev);
	platform_driver_unregister(&alix_mouse_driver);
}

module_init(alix_mouse_init);
module_exit(alix_mouse_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AlixOS Project");
MODULE_DESCRIPTION("Amiga mouse input driver via PiStorm32-lite");
MODULE_ALIAS("platform:alix_mouse");
