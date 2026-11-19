/* SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause */
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#ifndef _DT_BINDINGS_POWER_MT6895_POWER_H
#define _DT_BINDINGS_POWER_MT6895_POWER_H

/*
 * Urutan dan nomor indeks WAJIB sama dengan firmware stock
 * (mt6895_scpsys / scp_domain_data_mt6895, 39 domain).
 *
 * DT yang dipakai perangkat adalah DT stock, dan DT mereferensikan domain
 * lewat nomor ini (power-domains = <&scpsys N>).  Menyisipkan satu domain
 * di tengah (mis. ufs0_shutdown di indeks 2) menggeser semua domain
 * sesudahnya, sehingga permintaan "isp_main" (N=7) berubah menjadi
 * isp_vcore: ISP_MAIN tidak pernah dinyalakan, lalu SMI common IMG
 * menulis CG register imgsys_main -> DEVAPC violation -> BUG.
 */
#define MT6895_POWER_DOMAIN_MD				0
#define MT6895_POWER_DOMAIN_CONN			1
#define MT6895_POWER_DOMAIN_ADSP_INFRA			2
#define MT6895_POWER_DOMAIN_AUDIO			3
#define MT6895_POWER_DOMAIN_ADSP_TOP_DORMANT		4
#define MT6895_POWER_DOMAIN_MM_INFRA			5
#define MT6895_POWER_DOMAIN_ISP_VCORE			6
#define MT6895_POWER_DOMAIN_ISP_MAIN			7
#define MT6895_POWER_DOMAIN_ISP_DIP1			8
#define MT6895_POWER_DOMAIN_ISP_IPE			9
#define MT6895_POWER_DOMAIN_CAM_VCORE			10
#define MT6895_POWER_DOMAIN_CAM_MAIN			11
#define MT6895_POWER_DOMAIN_CAM_MRAW			12
#define MT6895_POWER_DOMAIN_CAM_SUBA			13
#define MT6895_POWER_DOMAIN_CAM_SUBB			14
#define MT6895_POWER_DOMAIN_CAM_SUBC			15
#define MT6895_POWER_DOMAIN_MDP0			16
#define MT6895_POWER_DOMAIN_MDP1			17
#define MT6895_POWER_DOMAIN_DISP			18
#define MT6895_POWER_DOMAIN_DISP1			19
#define MT6895_POWER_DOMAIN_VDE0			20
#define MT6895_POWER_DOMAIN_VDE1			21
#define MT6895_POWER_DOMAIN_VEN0			22
#define MT6895_POWER_DOMAIN_VEN1			23
#define MT6895_POWER_DOMAIN_MM_PROC_DORMANT		24
#define MT6895_POWER_DOMAIN_DP_TX			25
#define MT6895_POWER_DOMAIN_MFG1			26
#define MT6895_POWER_DOMAIN_MFG2			27
#define MT6895_POWER_DOMAIN_MFG3			28
#define MT6895_POWER_DOMAIN_MFG4			29
#define MT6895_POWER_DOMAIN_MFG5			30
#define MT6895_POWER_DOMAIN_MFG6			31
#define MT6895_POWER_DOMAIN_MFG7			32
#define MT6895_POWER_DOMAIN_MFG8			33
#define MT6895_POWER_DOMAIN_MFG9			34
#define MT6895_POWER_DOMAIN_MFG10			35
#define MT6895_POWER_DOMAIN_MFG11			36
#define MT6895_POWER_DOMAIN_MFG12			37
#define MT6895_POWER_DOMAIN_APU				38
#define MT6895_POWER_DOMAIN_NR				39

#endif /* _DT_BINDINGS_POWER_MT6895_POWER_H */
