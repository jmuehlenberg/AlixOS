// SPDX-License-Identifier: GPL-2.0
/*
 * alix_parallel.c - AlixOS Amiga Parallel Port Driver
 *
 * Provides a character device (/dev/alix-parport) for the Amiga 1200's
 * built-in parallel port, accessed through the PiStorm32-lite bus interface.
 *
 * Hardware used:
 *   - CIA-A PRB  ($BFE101): 8-bit parallel data register
 *   - CIA-A DDRB ($BFE301): Data direction for parallel data (0=in, 1=out)
 *   - CIA-B PRA  ($BFD000): Control/status signals:
 *       Bit 0: BUSY   (active low, from peripheral)
 *       Bit 1: POUT   (Paper Out, from peripheral)
 *       Bit 2: SEL    (Select, active low, to peripheral)
 *
 * The Amiga parallel port is directly memory-mapped via CIA-A Port B.
 * Unlike PC parallel ports, it's a simple 8-bit bidirectional I/O port
 * with three control/status lines on CIA-B PRA.
 *
 * The driver implements a character device with:
 *   - write(): Send bytes out the parallel port (Centronics-style)
 *   - read():  Read bytes from the parallel port (input mode)
 *   - ioctl(): Set direction, read status lines
 *
 * Reference: Amiga Hardware Reference Manual, Chapter 7 (Parallel Port)
 */

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/ioctl.h>
#include "pistorm_bus.h"
#include "amiga_hwreg.h"

#define DRIVER_NAME       "alix_parallel"

/* ioctl commands */
#define ALIX_PAR_IOCTL_MAGIC   'P'
#define ALIX_PAR_SET_DIR       _IOW(ALIX_PAR_IOCTL_MAGIC, 1, int) /* 0=input, 1=output */
#define ALIX_PAR_GET_STATUS    _IOR(ALIX_PAR_IOCTL_MAGIC, 2, int) /* BUSY, POUT, SEL */
#define ALIX_PAR_SET_SELECT    _IOW(ALIX_PAR_IOCTL_MAGIC, 3, int) /* 0=deassert, 1=assert */

/* Status bits returned by GET_STATUS */
#define ALIX_PAR_STATUS_BUSY   (1 << 0)
#define ALIX_PAR_STATUS_POUT   (1 << 1)
#define ALIX_PAR_STATUS_SEL    (1 << 2)

struct alix_parallel {
	struct miscdevice misc;
	bool direction_out;   /* true = output mode, false = input mode */
	struct mutex lock;    /* Serialize userspace access */
};

static struct alix_parallel *alix_par;
static struct platform_device *alix_par_pdev;

/* ---- Hardware helpers ------------------------------------------------- */

static void alix_parallel_set_direction(bool output)
{
	pistorm_bus_lock();
	if (output)
		pistorm_write8(AMIGA_CIAA_DDRB, 0xFF);  /* All outputs */
	else
		pistorm_write8(AMIGA_CIAA_DDRB, 0x00);  /* All inputs */
	pistorm_bus_unlock();
}

static void alix_parallel_write_byte(u8 data)
{
	pistorm_bus_lock();
	pistorm_write8(AMIGA_CIAA_PRB, data);
	pistorm_bus_unlock();
}

static u8 alix_parallel_read_byte(void)
{
	u8 data;

	pistorm_bus_lock();
	data = pistorm_read8(AMIGA_CIAA_PRB);
	pistorm_bus_unlock();

	return data;
}

static int alix_parallel_get_status(void)
{
	u8 pra;
	int status = 0;

	pistorm_bus_lock();
	pra = pistorm_read8(AMIGA_CIAB_PRA);
	pistorm_bus_unlock();

	/* Invert active-low signals for a logical view */
	if (!(pra & CIAB_PRA_BUSY))
		status |= ALIX_PAR_STATUS_BUSY;
	if (pra & CIAB_PRA_POUT)
		status |= ALIX_PAR_STATUS_POUT;
	if (!(pra & CIAB_PRA_SEL))
		status |= ALIX_PAR_STATUS_SEL;

	return status;
}

static void alix_parallel_set_select(bool active)
{
	u8 pra;

	pistorm_bus_lock();
	pra = pistorm_read8(AMIGA_CIAB_PRA);
	if (active)
		pra &= ~CIAB_PRA_SEL;  /* Active low */
	else
		pra |= CIAB_PRA_SEL;
	pistorm_write8(AMIGA_CIAB_PRA, pra);
	pistorm_bus_unlock();
}

/**
 * Centronics-style byte output with BUSY handshake.
 * Returns 0 on success, -ETIMEDOUT if peripheral stays busy.
 */
static int alix_parallel_strobe_byte(u8 data)
{
	int timeout = 1000;  /* 1000 x 10us = 10ms timeout */
	u8 pra;

	/* Wait for BUSY to deassert (active low, so wait for high) */
	while (timeout-- > 0) {
		pistorm_bus_lock();
		pra = pistorm_read8(AMIGA_CIAB_PRA);
		pistorm_bus_unlock();

		if (pra & CIAB_PRA_BUSY)
			break;
		udelay(10);
	}

	if (timeout <= 0)
		return -ETIMEDOUT;

	/* Place data on the bus */
	alix_parallel_write_byte(data);

	/*
	 * The Amiga parallel port doesn't have a dedicated STROBE pin
	 * directly accessible. Most Amiga software uses CIA-B handshake
	 * mode or bit-bangs timing. We use a brief delay to ensure the
	 * data is stable on the bus, which works for most peripherals.
	 */
	udelay(1);

	return 0;
}

/* ---- File operations -------------------------------------------------- */

static int alix_parallel_fop_open(struct inode *inode, struct file *filp)
{
	struct alix_parallel *par = alix_par;

	if (!par)
		return -ENODEV;

	filp->private_data = par;

	/* Default: output mode */
	mutex_lock(&par->lock);
	par->direction_out = true;
	alix_parallel_set_direction(true);
	mutex_unlock(&par->lock);

	return 0;
}

static int alix_parallel_fop_release(struct inode *inode, struct file *filp)
{
	struct alix_parallel *par = filp->private_data;

	/* Set port to input mode when closed (safe default) */
	mutex_lock(&par->lock);
	par->direction_out = false;
	alix_parallel_set_direction(false);
	mutex_unlock(&par->lock);

	return 0;
}

static ssize_t alix_parallel_fop_write(struct file *filp,
				       const char __user *buf,
				       size_t count, loff_t *ppos)
{
	struct alix_parallel *par = filp->private_data;
	unsigned char kbuf[256];
	size_t written = 0;
	int err;

	mutex_lock(&par->lock);

	if (!par->direction_out) {
		mutex_unlock(&par->lock);
		return -EINVAL;
	}

	while (written < count) {
		size_t chunk = min(count - written, sizeof(kbuf));
		size_t i;

		if (copy_from_user(kbuf, buf + written, chunk)) {
			mutex_unlock(&par->lock);
			return written ? written : -EFAULT;
		}

		for (i = 0; i < chunk; i++) {
			err = alix_parallel_strobe_byte(kbuf[i]);
			if (err) {
				mutex_unlock(&par->lock);
				return written ? written : err;
			}
			written++;
		}
	}

	mutex_unlock(&par->lock);
	return written;
}

static ssize_t alix_parallel_fop_read(struct file *filp,
				      char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct alix_parallel *par = filp->private_data;
	unsigned char kbuf[256];
	size_t to_read, i;

	mutex_lock(&par->lock);

	if (par->direction_out) {
		mutex_unlock(&par->lock);
		return -EINVAL;
	}

	to_read = min(count, sizeof(kbuf));
	for (i = 0; i < to_read; i++)
		kbuf[i] = alix_parallel_read_byte();

	mutex_unlock(&par->lock);

	if (copy_to_user(buf, kbuf, to_read))
		return -EFAULT;

	return to_read;
}

static long alix_parallel_fop_ioctl(struct file *filp, unsigned int cmd,
				    unsigned long arg)
{
	struct alix_parallel *par = filp->private_data;
	int val;

	switch (cmd) {
	case ALIX_PAR_SET_DIR:
		if (get_user(val, (int __user *)arg))
			return -EFAULT;
		mutex_lock(&par->lock);
		par->direction_out = !!val;
		alix_parallel_set_direction(par->direction_out);
		mutex_unlock(&par->lock);
		return 0;

	case ALIX_PAR_GET_STATUS:
		val = alix_parallel_get_status();
		if (put_user(val, (int __user *)arg))
			return -EFAULT;
		return 0;

	case ALIX_PAR_SET_SELECT:
		if (get_user(val, (int __user *)arg))
			return -EFAULT;
		alix_parallel_set_select(!!val);
		return 0;

	default:
		return -ENOTTY;
	}
}

static const struct file_operations alix_parallel_fops = {
	.owner          = THIS_MODULE,
	.open           = alix_parallel_fop_open,
	.release        = alix_parallel_fop_release,
	.write          = alix_parallel_fop_write,
	.read           = alix_parallel_fop_read,
	.unlocked_ioctl = alix_parallel_fop_ioctl,
};

/* ---- Platform driver -------------------------------------------------- */

static int alix_parallel_probe(struct platform_device *pdev)
{
	struct alix_parallel *par;
	int err;

	par = devm_kzalloc(&pdev->dev, sizeof(*par), GFP_KERNEL);
	if (!par)
		return -ENOMEM;

	mutex_init(&par->lock);
	par->direction_out = false;

	par->misc.minor = MISC_DYNAMIC_MINOR;
	par->misc.name  = "alix-parport";
	par->misc.fops  = &alix_parallel_fops;

	err = misc_register(&par->misc);
	if (err) {
		dev_err(&pdev->dev, "Failed to register misc device\n");
		return err;
	}

	/* Set port to input mode initially (safe default) */
	alix_parallel_set_direction(false);

	alix_par = par;
	platform_set_drvdata(pdev, par);

	dev_info(&pdev->dev,
		 "AlixOS Amiga parallel port driver loaded (/dev/alix-parport)\n");
	return 0;
}

static int alix_parallel_remove(struct platform_device *pdev)
{
	struct alix_parallel *par = platform_get_drvdata(pdev);

	/* Set to input mode before unloading */
	alix_parallel_set_direction(false);

	misc_deregister(&par->misc);
	alix_par = NULL;

	dev_info(&pdev->dev, "AlixOS Amiga parallel port driver unloaded\n");
	return 0;
}

static struct platform_driver alix_parallel_driver = {
	.probe  = alix_parallel_probe,
	.remove = alix_parallel_remove,
	.driver = {
		.name = DRIVER_NAME,
	},
};

/* ---- Module init/exit ------------------------------------------------- */

static int __init alix_parallel_init(void)
{
	int err;

	err = platform_driver_register(&alix_parallel_driver);
	if (err)
		return err;

	alix_par_pdev = platform_device_register_simple(DRIVER_NAME, -1,
							NULL, 0);
	if (IS_ERR(alix_par_pdev)) {
		platform_driver_unregister(&alix_parallel_driver);
		return PTR_ERR(alix_par_pdev);
	}

	return 0;
}

static void __exit alix_parallel_exit(void)
{
	platform_device_unregister(alix_par_pdev);
	platform_driver_unregister(&alix_parallel_driver);
}

module_init(alix_parallel_init);
module_exit(alix_parallel_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AlixOS Project");
MODULE_DESCRIPTION("Amiga parallel port driver via PiStorm32-lite");
MODULE_ALIAS("platform:alix_parallel");
