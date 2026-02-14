// SPDX-License-Identifier: GPL-2.0
/*
 * pistorm_bus_kmod.c - PiStorm32-lite bus access kernel module
 *
 * Module init/exit and debugfs interface for testing bus access.
 * Loads as pistorm_bus.ko and exports read/write symbols for
 * input driver modules (alix_kbd, alix_mouse, alix_joy).
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include "pistorm_bus.h"

static struct dentry *dbg_dir;

/* debugfs state for interactive register access */
static u32 dbg_addr;
static u8 dbg_size = 8;  /* 8=byte, 16=word, 32=long */

/*
 * debugfs: read register address
 * Write a hex address to set the target, e.g.: echo 0xBFE001 > addr
 */
static ssize_t dbg_addr_write(struct file *f, const char __user *buf,
			      size_t len, loff_t *off)
{
	char tmp[32];
	size_t n = min(len, sizeof(tmp) - 1);

	if (copy_from_user(tmp, buf, n))
		return -EFAULT;
	tmp[n] = '\0';

	if (kstrtou32(tmp, 0, &dbg_addr))
		return -EINVAL;

	return len;
}

static ssize_t dbg_addr_read(struct file *f, char __user *buf,
			     size_t len, loff_t *off)
{
	char tmp[32];
	int n = snprintf(tmp, sizeof(tmp), "0x%06X\n", dbg_addr);

	return simple_read_from_buffer(buf, len, off, tmp, n);
}

static const struct file_operations dbg_addr_fops = {
	.write = dbg_addr_write,
	.read  = dbg_addr_read,
};

/*
 * debugfs: set access size (8, 16, or 32)
 */
static ssize_t dbg_size_write(struct file *f, const char __user *buf,
			      size_t len, loff_t *off)
{
	char tmp[16];
	unsigned int val;
	size_t n = min(len, sizeof(tmp) - 1);

	if (copy_from_user(tmp, buf, n))
		return -EFAULT;
	tmp[n] = '\0';

	if (kstrtouint(tmp, 0, &val))
		return -EINVAL;

	if (val != 8 && val != 16 && val != 32)
		return -EINVAL;

	dbg_size = val;
	return len;
}

static ssize_t dbg_size_read(struct file *f, char __user *buf,
			     size_t len, loff_t *off)
{
	char tmp[16];
	int n = snprintf(tmp, sizeof(tmp), "%u\n", dbg_size);

	return simple_read_from_buffer(buf, len, off, tmp, n);
}

static const struct file_operations dbg_size_fops = {
	.write = dbg_size_write,
	.read  = dbg_size_read,
};

/*
 * debugfs: read value from current address
 * cat value  -> reads from dbg_addr with dbg_size
 */
static ssize_t dbg_value_read(struct file *f, char __user *buf,
			      size_t len, loff_t *off)
{
	char tmp[32];
	int n;
	u32 val;

	pistorm_bus_lock();

	switch (dbg_size) {
	case 8:
		val = pistorm_read8(dbg_addr);
		n = snprintf(tmp, sizeof(tmp), "0x%02X\n", val);
		break;
	case 16:
		val = pistorm_read16(dbg_addr);
		n = snprintf(tmp, sizeof(tmp), "0x%04X\n", val);
		break;
	case 32:
		val = pistorm_read32(dbg_addr);
		n = snprintf(tmp, sizeof(tmp), "0x%08X\n", val);
		break;
	default:
		n = snprintf(tmp, sizeof(tmp), "error\n");
		break;
	}

	pistorm_bus_unlock();

	return simple_read_from_buffer(buf, len, off, tmp, n);
}

/*
 * debugfs: write value to current address
 * echo 0xFF > value  -> writes to dbg_addr with dbg_size
 */
static ssize_t dbg_value_write(struct file *f, const char __user *buf,
			       size_t len, loff_t *off)
{
	char tmp[32];
	u32 val;
	size_t n = min(len, sizeof(tmp) - 1);

	if (copy_from_user(tmp, buf, n))
		return -EFAULT;
	tmp[n] = '\0';

	if (kstrtou32(tmp, 0, &val))
		return -EINVAL;

	pistorm_bus_lock();

	switch (dbg_size) {
	case 8:
		pistorm_write8(dbg_addr, val & 0xFF);
		break;
	case 16:
		pistorm_write16(dbg_addr, val & 0xFFFF);
		break;
	case 32:
		pistorm_write32(dbg_addr, val);
		break;
	}

	pistorm_bus_unlock();

	return len;
}

static const struct file_operations dbg_value_fops = {
	.read  = dbg_value_read,
	.write = dbg_value_write,
};

/* ---- Module init/exit ------------------------------------------------ */

static int __init pistorm_bus_module_init(void)
{
	int ret;

	ret = pistorm_bus_init();
	if (ret)
		return ret;

	/* Create debugfs interface: /sys/kernel/debug/pistorm/ */
	dbg_dir = debugfs_create_dir("pistorm", NULL);
	if (IS_ERR_OR_NULL(dbg_dir)) {
		pr_warn("pistorm_bus: debugfs not available, continuing without it\n");
		dbg_dir = NULL;
	} else {
		debugfs_create_file("addr", 0644, dbg_dir, NULL, &dbg_addr_fops);
		debugfs_create_file("size", 0644, dbg_dir, NULL, &dbg_size_fops);
		debugfs_create_file("value", 0644, dbg_dir, NULL, &dbg_value_fops);
		pr_info("pistorm_bus: debugfs interface at /sys/kernel/debug/pistorm/\n");
	}

	pr_info("pistorm_bus: AlixOS PiStorm32-lite bus driver loaded\n");
	return 0;
}

static void __exit pistorm_bus_module_exit(void)
{
	if (dbg_dir)
		debugfs_remove_recursive(dbg_dir);

	pistorm_bus_exit();

	pr_info("pistorm_bus: AlixOS PiStorm32-lite bus driver unloaded\n");
}

module_init(pistorm_bus_module_init);
module_exit(pistorm_bus_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AlixOS Project");
MODULE_DESCRIPTION("PiStorm32-lite bus access driver for Amiga 1200");
MODULE_ALIAS("platform:pistorm_bus");
