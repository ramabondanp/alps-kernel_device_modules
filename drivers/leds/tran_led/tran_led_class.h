/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Transsion Inc.
 *
 * Transsion LED class interface.
 *
 * This is the public half of tran-led-core: it is the contract that the
 * per-chip LED drivers (leds-aw22xxx, leds-hk32f0301, ...) compile against.
 *
 * Transsion never released this tree. It is reconstructed from the X6871
 * firmware tran-led-core.ko (PLAN.md 6.2q), which is not stripped and whose
 * import set ends up exactly identically reconstructed here.
 *
 * The struct layout is not a guess. It was measured twice, from two
 * independent .ko files, and the two measurements agree:
 *
 *   a) tran-led-core.ko: every accessor loads the ops pointer from
 *      `dev+0x08` and then a fixed slot:
 *        0x00 set_i2c_retries   0x38 store_tran_led_cmd
 *        0x08 set_i2c_timeout   0x40 show_tran_led_cmd
 *        0x10 rw_i2c_data       0x48 store_tran_led_check
 *        0x18 get_chip_type     0x50 show_tran_led_check
 *        0x20 get_cmd           0x58 store_tran_multicolor
 *        0x28 set_cmd           0x60 show_tran_multicolor
 *        0x30 (undocumented, only used by the ioctl path)
 *
 *   b) leds-aw22xxx.ko: its const ops table lives in .rodata at +0x140 and
 *      its seven non-NULL entries land on exactly 0x158/0x160/0x168/0x178/
 *      0x180/0x198/0x1a0 -> slots 0x18/0x20/0x28/0x38/0x40/0x58/0x60, which
 *      is byte-for-byte the same layout.
 *
 * The two .ko files agree, so the layout is fixed by evidence rather than
 * inferred from the 6.1 donor header, which describes an older and much
 * smaller structure (4 slots, ops at 0x08 but no cmd/chip-type/multicolor).
 */

#ifndef __TRAN_LED_CLASS_H
#define __TRAN_LED_CLASS_H

#include <linux/device.h>
#include <linux/leds.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/stddef.h>

struct tran_led_device;

/**
 * struct tran_led_ops - operations a LED chip driver provides
 * @set_i2c_retries: set the I2C retry count
 * @set_i2c_timeout: set the I2C timeout in ms
 * @rw_i2c_data:     raw register read/write, arg is a user pointer
 * @get_chip_type:   report the chip id
 * @get_cmd:         read the pending command buffer (32 bytes)
 * @set_cmd:         push a command buffer (32 bytes)
 * @store_raw:       slot 0x30, only reachable through the ioctl path
 * @store_tran_led_cmd:   sysfs tran_led_cmd write
 * @show_tran_led_cmd:    sysfs tran_led_cmd read
 * @store_tran_led_check: sysfs tran_led_check write
 * @show_tran_led_check:  sysfs tran_led_check read
 * @store_tran_multicolor: sysfs tran_multicolor write
 * @show_tran_multicolor:  sysfs tran_multicolor read
 */
struct tran_led_ops {
	int (*set_i2c_retries)(struct tran_led_device *dev, int retries);
	int (*set_i2c_timeout)(struct tran_led_device *dev, int ms);
	int (*rw_i2c_data)(struct tran_led_device *dev, unsigned long arg);
	int (*get_chip_type)(struct tran_led_device *dev, int *type);
	int (*get_cmd)(struct tran_led_device *dev, char *buf, int len);
	int (*set_cmd)(struct tran_led_device *dev, const char *buf, int len);
	int (*store_raw)(struct tran_led_device *dev, const char *buf);
	int (*store_tran_led_cmd)(struct tran_led_device *dev, const char *cmd);
	int (*show_tran_led_cmd)(struct tran_led_device *dev, char *buf);
	int (*store_tran_led_check)(struct tran_led_device *dev, const char *cmd);
	int (*show_tran_led_check)(struct tran_led_device *dev, char *buf);
	int (*store_tran_multicolor)(struct tran_led_device *dev, const char *cmd);
	int (*show_tran_multicolor)(struct tran_led_device *dev, char *buf);
};

struct tran_led_properties {
	const char *alias_name;
};

/*
 * Offset dev to +0x220 and let the compiler place driver_data at +0x2b8 and
 * class at +0x538, exactly as the firmware does.
 *
 * Measured from tran_led_device_register, which stores
 *   parent   at +0x280  (dev+0x60)
 *   devdata  at +0x2b8  (dev+0x98, i.e. dev.driver_data)
 *   class    at +0x538  (dev+0x318, i.e. dev.class)
 * and from get_leddev_by_name, which converts a struct device * back with
 * `sub x8, x0, #0x220`.
 *
 * This matters at runtime: leds-aw22xxx reads its private data straight from
 * [led_dev + 0x2b8], so a struct that put dev at +0x28 would corrupt it.
 */
struct tran_led_device {
	struct tran_led_properties props;	/* +0x00 */
	const struct tran_led_ops *ops;		/* +0x08 */
	struct platform_device *pdev;		/* +0x10 */
	u8 __pad[0x220 - 3 * sizeof(void *)];	/* +0x18 .. +0x21f */

	struct device dev;			/* +0x220 */
	struct led_classdev cdev;
};

_Static_assert(offsetof(struct tran_led_device, ops) == 0x08,
	       "ops must stay at +0x08");
_Static_assert(offsetof(struct tran_led_device, dev) == 0x220,
	       "dev must stay at +0x220");
_Static_assert(offsetof(struct tran_led_device, dev.driver_data) == 0x2b8,
	       "dev.driver_data must stay at +0x2b8");

struct tran_led_device *tran_led_device_register(const char *name,
						 struct device *parent,
						 void *devdata,
						 const struct tran_led_ops *ops,
						 const struct tran_led_properties *props);
void tran_led_device_unregister(struct tran_led_device *led_dev);
struct tran_led_device *get_leddev_by_name(const char *name);

int tran_led_dev_set_i2c_retries(struct tran_led_device *dev, int retries);
int tran_led_dev_set_i2c_timeout(struct tran_led_device *dev, int ms);
int tran_led_dev_rw_i2c_data(struct tran_led_device *dev, unsigned long arg);
int tran_led_dev_get_chip_type(struct tran_led_device *dev, int *type);
int tran_led_dev_get_cmd(struct tran_led_device *dev, char *buf, int len);
int tran_led_dev_set_cmd(struct tran_led_device *dev, const char *buf, int len);
int tran_led_dev_store_tran_led_cmd(struct tran_led_device *dev, const char *cmd);
int tran_led_dev_show_tran_led_cmd(struct tran_led_device *dev, char *buf);
int tran_led_dev_store_tran_led_check(struct tran_led_device *dev, const char *cmd);
int tran_led_dev_show_tran_led_check(struct tran_led_device *dev, char *buf);
int tran_led_dev_store_tran_multicolor(struct tran_led_device *dev, const char *cmd);
int tran_led_dev_show_tran_multicolor(struct tran_led_device *dev, char *buf);

int tlc_get_debug_level(void);

#define TLC_ERROR_LEVEL	0
#define TLC_INFO_LEVEL	1
#define TLC_DEBUG_LEVEL	2

#define tlc_err(fmt, args...)						\
do {									\
	if (tlc_get_debug_level() >= TLC_ERROR_LEVEL)			\
		pr_notice("[tlc]" fmt, ##args);				\
} while (0)

#define tlc_info(fmt, args...)						\
do {									\
	if (tlc_get_debug_level() >= TLC_INFO_LEVEL)			\
		pr_notice("[tlc]" fmt, ##args);				\
} while (0)

#define tlc_dbg(fmt, args...)						\
do {									\
	if (tlc_get_debug_level() >= TLC_DEBUG_LEVEL)			\
		pr_notice("[tlc]" fmt, ##args);				\
} while (0)

#define to_tran_led_device(obj) container_of(obj, struct tran_led_device, dev)

#endif /* __TRAN_LED_CLASS_H */
