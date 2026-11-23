// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Transsion Inc.
 */

#ifndef __AW22XXX_H__
#define __AW22XXX_H__

#include <linux/atomic.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>

/*********************************************************
 *
 * i2c
 *
 ********************************************************/
#define MAX_I2C_BUFFER_SIZE		65536

#define AW22XXX_FLASH_I2C_WRITES
#define MAX_FLASH_WRITE_BYTE_SIZE	128

#define AWINIC_FW_UPDATE_DELAY

#define AW22XXX_I2C_NAME		"aw22xxx_led"
#define AW22XXX_DRIVER_VERSION		"V1.3.0"

#define AW22XXX_CFG_NAME_MAX		64
#define AW22XXX_IMAX_NAME_MAX		32
#define AW22XXX_RGB_MAX			9
#define AW22XXX_CMD_SIZE		32
#define AW22XXX_MUSIC_GAIN_COUNT	16

/*********************************************************
 *
 * chip info
 *
 ********************************************************/
#define AW22XXX_SRSTR		0x76
#define AW22XXX_SRSTW		0x55
#define AW22118_CHIPID		0x18
#define AW22127_CHIPID		0x27

/*
 * The firmware reports these two values, not the register values.  The LED
 * HAL compares them against the enum in this header, so the numbers matter:
 * probe stores 5 for AW22118 and 6 for AW22127 (PLAN.md 6.2q).
 */
enum aw22xxx_chipids {
	AW22XXX = 0,
	AW22118 = 5,
	AW22127 = 6,
};

enum aw22xxx_fw_flags {
	AW22XXX_FLAG_FW_NONE = 0,
	AW22XXX_FLAG_FW_UPDATE = 1,
	AW22XXX_FLAG_FW_OK = 2,
	AW22XXX_FLAG_FW_FAIL = 3,
};

enum aw22xxx_flags {
	AW22XXX_FLAG_NONE = 0,
	AW22XXX_FLAG_SKIP_INTERRUPTS = 1,
};

enum aw22xxx_reg_page {
	AW22XXX_REG_PAGE0 = 0,
	AW22XXX_REG_PAGE1 = 1,
	AW22XXX_REG_PAGE2 = 2,
	AW22XXX_REG_PAGE3 = 3,
	AW22XXX_REG_PAGE4 = 4,
};

enum aw22xxx_dbgctr {
	AW22XXX_DBGCTR_NORMAL = 0,
	AW22XXX_DBGCTR_IRAM = 1,
	AW22XXX_DBGCTR_SFR = 2,
	AW22XXX_DBGCTR_FLASH = 3,
	AW22XXX_DBGCTR_MAX = 4,
};

enum aw22xxx_imax {
	AW22XXX_IMAX_2mA = 8,
	AW22XXX_IMAX_3mA = 0,
	AW22XXX_IMAX_4mA = 9,
	AW22XXX_IMAX_6mA = 1,
	AW22XXX_IMAX_9mA = 2,
	AW22XXX_IMAX_10mA = 11,
	AW22XXX_IMAX_15mA = 3,
	AW22XXX_IMAX_20mA = 12,
	AW22XXX_IMAX_30mA = 4,
	AW22XXX_IMAX_40mA = 14,
	AW22XXX_IMAX_45mA = 5,
	AW22XXX_IMAX_60mA = 6,
	AW22XXX_IMAX_75mA = 7,
};

/*
 * Effect numbers are indexes into aw22xxx_cfg_name.  Each scheme/audio group
 * has 8 (scheme4: 9) entries; the _A and _B firmware sets are +66 and +132,
 * selected by the multi_color_type.  The rest of the enum is the tail of the
 * table (factory / off / performance), verified against the .data array.
 */
enum aw22xxx_effect {
	LIGHT_SCHEME1_1 = 0,
	LIGHT_SCHEME1_8 = 7,
	LIGHT_SCHEME2_1 = 8,
	LIGHT_SCHEME2_8 = 15,
	LIGHT_SCHEME3_1 = 16,
	LIGHT_SCHEME3_8 = 23,
	LIGHT_SCHEME4_1 = 24,
	LIGHT_SCHEME4_9 = 32,
	LIGHT_AUDIO1_1 = 33,
	LIGHT_AUDIO1_8 = 40,
	LIGHT_AUDIO2_1 = 41,
	LIGHT_AUDIO2_8 = 48,
	LIGHT_AUDIO3_1 = 49,
	LIGHT_AUDIO3_8 = 56,
	LIGHT_AUDIO4_1 = 57,
	LIGHT_AUDIO4_9 = 65,
	LIGHT_FACTORY_RED = 198,
	LIGHT_FACTORY_GREEN = 199,
	LIGHT_FACTORY_BLUE = 200,
	LIGHT_FACTORY_WHITE = 201,
	LIGHT_LEDS_OFF = 202,
	LIGHT_PERFORMANCE_1 = 203,
	LIGHT_PERFORMANCE_2 = 204,
	LIGHT_PERFORMANCE_3 = 205,
	LIGHT_EFFECT_MAX = 206,
};

#define AW22XXX_MULTI_COLOR_SHIFT	0x42
#define AW22XXX_SCHEME_STRIDE		8

/*********************************************************
 *
 * cfg
 *
 ********************************************************/
#define AW22XXX_AUDIO_INITGAIN		0x1F

/*********************************************************
 *
 * struct
 *
 ********************************************************/
struct aw22xxx_container {
	unsigned int len;
	unsigned int version;
	unsigned int bist;
	unsigned int key;
	unsigned char data[];
};

struct aw22xxx_music_gain {
	unsigned int volume_level;
	unsigned int music_gain;
};

struct tran_led_device;

/*
 * Field order reproduces the firmware layout: the LED class device starts at
 * +0x10 and the probe/ops paths depend on the named offsets below.  There is
 * no external consumer of this struct (aw22xxx_cover_gain has no importer in
 * the 237-module firmware), but keeping the layout makes the .ko comparison
 * meaningful.  See PLAN.md 6.2q for the measuring method.
 */
struct aw22xxx {
	struct i2c_client *i2c;			/* +0x000 */
	struct device *dev;			/* +0x008 */
	struct led_classdev cdev;		/* +0x010 */
	struct work_struct brightness_work;	/* +0x1d0 */
	struct work_struct task_work;		/* +0x1f0 */
	struct work_struct fw_work;		/* +0x210 */
	struct work_struct cfg_work;		/* +0x230 */
	struct hrtimer fw_timer;		/* +0x250 */
	struct mutex cfg_lock;			/* +0x290 */

	int reset_gpio;				/* +0x2c0 */
	int irq_gpio;				/* +0x2c4 */

	unsigned char flags;			/* +0x2c8 */
	unsigned char chipid;			/* +0x2c9 */
	unsigned char fw_update;		/* +0x2ca */
	unsigned char fw_flags;			/* +0x2cb */
	unsigned int imax;			/* +0x2cc */
	unsigned int fw_version;		/* +0x2d0 */
	unsigned char reset_imax;		/* +0x2d4 */
	unsigned int audio_gain;		/* +0x2d8 */

	unsigned char task0;			/* +0x2dc */
	unsigned char task1;			/* +0x2dd */
	unsigned char effect;			/* +0x2de */
	unsigned char cfg;			/* +0x2df */

	unsigned int rgb[AW22XXX_RGB_MAX];	/* +0x2e0 */

	atomic_t soft_show_en;			/* +0x304 */
	atomic_t lamp_init_done;		/* +0x308 */

	unsigned int multi_color_count;		/* +0x30c */
	unsigned int multi_color_type;		/* +0x310 */
	unsigned int multi_color_shift;		/* +0x314 */
	const char **multi_color_array;		/* +0x318 */
	const char *tran_name;			/* +0x320 */
	struct tran_led_device *led_dev;	/* +0x328 */
	struct aw22xxx_music_gain *music_gain;	/* +0x330 */
	unsigned int music_gain_count;		/* +0x338 */
	unsigned int chip_type;			/* +0x33c */
	unsigned char cmd[AW22XXX_CMD_SIZE];	/* +0x340 */
};


int aw22xxx_cover_gain(struct aw22xxx *aw22xxx, unsigned int volume_level);

#endif /* __AW22XXX_H__ */
