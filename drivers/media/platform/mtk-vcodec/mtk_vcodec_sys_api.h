/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#ifndef _MTK_VCODEC_SYS_API_H_
#define _MTK_VCODEC_SYS_API_H_

#include <linux/types.h>

struct task_struct;

/*
 * Function table installed by mtk-vcodec-sys-api. Only
 * set_task_priority is used by the hardware codec driver today; keep the
 * remaining slots so the layout stays identical to the vendor ABI.
 */
struct mtk_vcodec_sys_api_funcs {
	void *reserved0;
	void *reserved1;
	void *reserved2;
	int (*set_task_priority)(struct task_struct *task, int prio);
	void *reserved3;
};

void mtk_vcodec_set_sys_api_funcs(struct mtk_vcodec_sys_api_funcs *funcs);
int mtk_vcodec_set_task_priority(struct task_struct *task, int prio);

#endif /* _MTK_VCODEC_SYS_API_H_ */
