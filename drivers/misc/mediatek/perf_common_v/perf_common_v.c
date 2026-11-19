// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 MediaTek Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sprintf.h>
#include <linux/types.h>

extern void (*format_sbin_data_hook)(char *buf, u32 size, u32 *sbin_data,
				     u32 lens);

static void format_sbin_data(char *buf, u32 size, u32 *sbin_data, u32 lens)
{
	char *ptr;
	char *end;
	int len;
	u32 i;

	len = snprintf(buf, size, "ARRAY[");
	if (len < 0 || len >= size)
		return;

	ptr = buf + len;
	end = buf + size;

	for (i = 0; i < lens; i++) {
		u32 data = sbin_data[i];

		len = snprintf(ptr, end - ptr, "%02x,%02x,%02x,%02x,",
			       data & 0xff, (data >> 16) & 0xff,
			       (data >> 8) & 0xff, (data >> 24) & 0xff);
		if (len < 0 || len >= end - ptr)
			return;
		ptr += len;
	}

	snprintf(ptr - 1, end - (ptr - 1), "]");
}

static void init_perf_common_hook(void)
{
	format_sbin_data_hook = format_sbin_data;
}

static int __init perf_common_v_init(void)
{
	init_perf_common_hook();
	pr_info("%s %d: perf_common hook done", __func__, __LINE__);
	return 0;
}

static void __exit perf_common_v_exit(void)
{
}

module_init(perf_common_v_init);
module_exit(perf_common_v_exit);

MODULE_LICENSE("Proprietary");
MODULE_DESCRIPTION("MediaTek perf_common_v");
MODULE_AUTHOR("MediaTek Inc.");
