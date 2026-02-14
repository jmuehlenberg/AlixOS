// SPDX-License-Identifier: GPL-2.0
/*
 * alix_rtc.c - AlixOS Amiga Real-Time Clock Driver
 *
 * Exposes the Amiga 1200's battery-backed Ricoh RP5C01A real-time clock
 * as a standard Linux RTC device (/dev/rtc0). This allows the Pi to read
 * the correct time at boot even without network/NTP access.
 *
 * Hardware:
 *   Ricoh RP5C01A at $DC0000, register stride 4 bytes, 4-bit data.
 *   Battery-backed — keeps time when Amiga is powered off.
 *
 * Register access:
 *   reg_addr = $DC0000 + (register_number * 4)
 *   Data in lower nibble (bits 3-0) only.
 *   Bank select via control register (reg 13), bits 1-0.
 *
 * Year handling:
 *   The RP5C01A stores a 2-digit BCD year (00-99).
 *   Interpretation: >= 78 → 1900+year, < 78 → 2000+year
 *   (matches standard Amiga convention, valid until 2077)
 *
 * Usage:
 *   # Read time
 *   hwclock -r -f /dev/rtc0
 *
 *   # Set system time from RTC
 *   hwclock -s -f /dev/rtc0
 *
 *   # Write system time to RTC
 *   hwclock -w -f /dev/rtc0
 *
 * Reference: Ricoh RP5C01A datasheet, Amiga Hardware Reference Manual
 */

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <linux/bcd.h>
#include "pistorm_bus.h"
#include "amiga_hwreg.h"

#define DRIVER_NAME "alix_rtc"

struct alix_rtc {
	struct rtc_device *rtc;
};

/* ---- Low-level register access ---------------------------------------- */

static u8 rtc_read_reg(int reg)
{
	return pistorm_read8(AMIGA_RTC_REG(reg)) & 0x0F;
}

static void rtc_write_reg(int reg, u8 val)
{
	pistorm_write8(AMIGA_RTC_REG(reg), val & 0x0F);
}

/* Select register bank (0-3). Preserves timer enable bit. */
static void rtc_select_bank(int bank)
{
	u8 ctrl = rtc_read_reg(RTC_CTRL);
	ctrl = (ctrl & ~0x03) | (bank & 0x03);
	rtc_write_reg(RTC_CTRL, ctrl);
}

/* ---- RTC operations --------------------------------------------------- */

static int alix_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	int year;

	pistorm_bus_lock();

	/* Select bank 0 (time registers), ensure timer is running */
	rtc_select_bank(0);

	tm->tm_sec  = rtc_read_reg(RTC_SEC10)  * 10 + rtc_read_reg(RTC_SEC1);
	tm->tm_min  = rtc_read_reg(RTC_MIN10)  * 10 + rtc_read_reg(RTC_MIN1);
	tm->tm_hour = rtc_read_reg(RTC_HR10)   * 10 + rtc_read_reg(RTC_HR1);
	tm->tm_mday = rtc_read_reg(RTC_DAY10)  * 10 + rtc_read_reg(RTC_DAY1);
	tm->tm_mon  = rtc_read_reg(RTC_MON10)  * 10 + rtc_read_reg(RTC_MON1) - 1;
	tm->tm_wday = rtc_read_reg(RTC_DOW);

	year = rtc_read_reg(RTC_YR10) * 10 + rtc_read_reg(RTC_YR1);

	pistorm_bus_unlock();

	/* 2-digit year: >= 78 → 19xx, < 78 → 20xx */
	if (year >= 78)
		tm->tm_year = year;         /* years since 1900 */
	else
		tm->tm_year = year + 100;   /* 2000+year - 1900 = year+100 */

	return rtc_valid_tm(tm);
}

static int alix_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	int year;

	/* Convert tm_year (years since 1900) to 2-digit */
	if (tm->tm_year >= 100)
		year = tm->tm_year - 100;   /* 2000+ → 0-99 */
	else
		year = tm->tm_year;         /* 1900+ → 0-99 */

	pistorm_bus_lock();

	/* Select bank 0, halt timer during write */
	rtc_select_bank(0);
	{
		u8 ctrl = rtc_read_reg(RTC_CTRL);
		rtc_write_reg(RTC_CTRL, ctrl & ~RTC_CTRL_TIMER);
	}

	rtc_write_reg(RTC_SEC1,  tm->tm_sec  % 10);
	rtc_write_reg(RTC_SEC10, tm->tm_sec  / 10);
	rtc_write_reg(RTC_MIN1,  tm->tm_min  % 10);
	rtc_write_reg(RTC_MIN10, tm->tm_min  / 10);
	rtc_write_reg(RTC_HR1,   tm->tm_hour % 10);
	rtc_write_reg(RTC_HR10,  tm->tm_hour / 10);
	rtc_write_reg(RTC_DAY1,  tm->tm_mday % 10);
	rtc_write_reg(RTC_DAY10, tm->tm_mday / 10);
	rtc_write_reg(RTC_MON1,  (tm->tm_mon + 1) % 10);
	rtc_write_reg(RTC_MON10, (tm->tm_mon + 1) / 10);
	rtc_write_reg(RTC_YR1,   year % 10);
	rtc_write_reg(RTC_YR10,  year / 10);
	rtc_write_reg(RTC_DOW,   tm->tm_wday);

	/* Restart timer */
	{
		u8 ctrl = rtc_read_reg(RTC_CTRL);
		rtc_write_reg(RTC_CTRL, ctrl | RTC_CTRL_TIMER);
	}

	pistorm_bus_unlock();

	return 0;
}

static const struct rtc_class_ops alix_rtc_ops = {
	.read_time = alix_rtc_read_time,
	.set_time  = alix_rtc_set_time,
};

/* ---- Platform driver -------------------------------------------------- */

static int alix_rtc_probe(struct platform_device *pdev)
{
	struct alix_rtc *artc;

	artc = devm_kzalloc(&pdev->dev, sizeof(*artc), GFP_KERNEL);
	if (!artc)
		return -ENOMEM;

	artc->rtc = devm_rtc_device_register(&pdev->dev, DRIVER_NAME,
					     &alix_rtc_ops, THIS_MODULE);
	if (IS_ERR(artc->rtc))
		return PTR_ERR(artc->rtc);

	/* Ensure timer is running */
	pistorm_bus_lock();
	rtc_select_bank(0);
	{
		u8 ctrl = rtc_read_reg(RTC_CTRL);
		if (!(ctrl & RTC_CTRL_TIMER))
			rtc_write_reg(RTC_CTRL, ctrl | RTC_CTRL_TIMER);
	}
	pistorm_bus_unlock();

	platform_set_drvdata(pdev, artc);

	dev_info(&pdev->dev, "AlixOS Amiga RTC (RP5C01A) registered\n");
	return 0;
}

static int alix_rtc_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "AlixOS Amiga RTC driver unloaded\n");
	return 0;
}

/* ---- Module plumbing -------------------------------------------------- */

static struct platform_device *alix_rtc_pdev;

static struct platform_driver alix_rtc_driver = {
	.probe  = alix_rtc_probe,
	.remove = alix_rtc_remove,
	.driver = {
		.name = DRIVER_NAME,
	},
};

static int __init alix_rtc_init(void)
{
	int err;

	err = platform_driver_register(&alix_rtc_driver);
	if (err)
		return err;

	alix_rtc_pdev = platform_device_register_simple(DRIVER_NAME, -1,
							NULL, 0);
	if (IS_ERR(alix_rtc_pdev)) {
		platform_driver_unregister(&alix_rtc_driver);
		return PTR_ERR(alix_rtc_pdev);
	}

	return 0;
}

static void __exit alix_rtc_exit(void)
{
	platform_device_unregister(alix_rtc_pdev);
	platform_driver_unregister(&alix_rtc_driver);
}

module_init(alix_rtc_init);
module_exit(alix_rtc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AlixOS Project");
MODULE_DESCRIPTION("Amiga RTC (Ricoh RP5C01A) driver via PiStorm32-lite");
MODULE_ALIAS("platform:alix_rtc");
