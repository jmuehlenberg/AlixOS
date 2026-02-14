// SPDX-License-Identifier: GPL-2.0
/*
 * alix_led.c - AlixOS Amiga Power LED Driver
 *
 * Exposes the Amiga 1200 front-panel power LED as a standard Linux
 * LED class device, enabling use of kernel LED triggers such as
 * "heartbeat", "disk-activity", "default-on", etc.
 *
 * Hardware:
 *   CIA-A PRA ($BFE001) bit 1 — Power LED (active low)
 *   0 = LED on, 1 = LED off
 *
 * The LED appears as /sys/class/leds/amiga::power
 *
 * Usage examples:
 *   # Turn on
 *   echo 1 > /sys/class/leds/amiga::power/brightness
 *
 *   # Heartbeat blink
 *   echo heartbeat > /sys/class/leds/amiga::power/trigger
 *
 *   # Disk activity
 *   echo disk-activity > /sys/class/leds/amiga::power/trigger
 *
 * Reference: Amiga Hardware Reference Manual, Chapter 7 (CIA)
 */

#include <linux/module.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include "pistorm_bus.h"
#include "amiga_hwreg.h"

#define DRIVER_NAME "alix_led"

static char *default_trigger = "default-on";
module_param(default_trigger, charp, 0444);
MODULE_PARM_DESC(default_trigger,
		 "Default LED trigger (default: default-on)");

struct alix_led {
	struct led_classdev cdev;
};

static void alix_led_brightness_set(struct led_classdev *cdev,
				    enum led_brightness brightness)
{
	u8 pra;

	pistorm_bus_lock();
	pra = pistorm_read8(AMIGA_CIAA_PRA);

	if (brightness == LED_OFF)
		pra |= CIAA_PRA_LED;    /* Bit set = LED off (active low) */
	else
		pra &= ~CIAA_PRA_LED;   /* Bit clear = LED on */

	pistorm_write8(AMIGA_CIAA_PRA, pra);
	pistorm_bus_unlock();
}

static enum led_brightness alix_led_brightness_get(struct led_classdev *cdev)
{
	u8 pra;

	pistorm_bus_lock();
	pra = pistorm_read8(AMIGA_CIAA_PRA);
	pistorm_bus_unlock();

	/* Active low: bit clear = on */
	return (pra & CIAA_PRA_LED) ? LED_OFF : LED_FULL;
}

/* ---- Platform driver -------------------------------------------------- */

static int alix_led_probe(struct platform_device *pdev)
{
	struct alix_led *led;
	int err;

	led = devm_kzalloc(&pdev->dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->cdev.name            = "amiga::power";
	led->cdev.brightness_set  = alix_led_brightness_set;
	led->cdev.brightness_get  = alix_led_brightness_get;
	led->cdev.max_brightness  = LED_FULL;
	led->cdev.default_trigger = default_trigger;

	err = devm_led_classdev_register(&pdev->dev, &led->cdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register LED device\n");
		return err;
	}

	platform_set_drvdata(pdev, led);

	dev_info(&pdev->dev,
		 "AlixOS Amiga power LED registered (trigger: %s)\n",
		 default_trigger);
	return 0;
}

static int alix_led_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "AlixOS Amiga power LED driver unloaded\n");
	return 0;
}

/* ---- Module plumbing -------------------------------------------------- */

static struct platform_device *alix_led_pdev;

static struct platform_driver alix_led_driver = {
	.probe  = alix_led_probe,
	.remove = alix_led_remove,
	.driver = {
		.name = DRIVER_NAME,
	},
};

static int __init alix_led_init(void)
{
	int err;

	err = platform_driver_register(&alix_led_driver);
	if (err)
		return err;

	alix_led_pdev = platform_device_register_simple(DRIVER_NAME, -1,
							NULL, 0);
	if (IS_ERR(alix_led_pdev)) {
		platform_driver_unregister(&alix_led_driver);
		return PTR_ERR(alix_led_pdev);
	}

	return 0;
}

static void __exit alix_led_exit(void)
{
	platform_device_unregister(alix_led_pdev);
	platform_driver_unregister(&alix_led_driver);
}

module_init(alix_led_init);
module_exit(alix_led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AlixOS Project");
MODULE_DESCRIPTION("Amiga power LED driver via PiStorm32-lite");
MODULE_ALIAS("platform:alix_led");
