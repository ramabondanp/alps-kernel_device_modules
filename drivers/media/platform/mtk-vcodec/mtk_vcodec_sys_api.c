// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sprintf.h>

#include "mtk_vcodec_sys_api.h"

extern int set_task_priority(struct task_struct *task, int prio);

static struct mtk_vcodec_sys_api_funcs funcs = {
	.set_task_priority = set_task_priority,
};

static int __init mtk_vcodec_sys_api_init(void)
{
	mtk_vcodec_set_sys_api_funcs(&funcs);
	pr_info("[MTK_V4L2_API] %s:%d: sys APIs init done\n", __func__, __LINE__);
	return 0;
}

static void __exit mtk_vcodec_sys_api_exit(void)
{
	mtk_vcodec_set_sys_api_funcs(NULL);
	pr_info("[MTK_V4L2_API] %s:%d: sys APIs remove done\n", __func__, __LINE__);
}

module_init(mtk_vcodec_sys_api_init);
module_exit(mtk_vcodec_sys_api_exit);

MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mediatek video codec dependency utility");
