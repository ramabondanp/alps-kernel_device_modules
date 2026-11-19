/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Transsion Inc.
 *
 * Transsion TNE kernel queue.
 *
 * Reconstructed from the X6871 tnek.ko (PLAN.md 6.2t).  The module exposes
 * a misc device (/dev/tnek) and a small kernel API used by the TEE driver:
 * a producer queues a fixed 0x418-byte node and user space reads it as a
 * 0x408-byte message (the list_head is stripped).
 */

#ifndef __TNEK_H__
#define __TNEK_H__

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>

#define TNEK_MSG_SIZE		0x408
#define TNEK_NODE_SIZE		0x418
#define TNEK_MAX_QUEUE		9
#define TNEK_TAG_SIZE		30
#define TNEK_NAME_SIZE		32
#define TNEK_DATA_SIZE		256

/*
 * On-wire/read layout.  tnek_read() copies from node+0x10, so this struct
 * starts at the node's tag field and is exactly 0x408 bytes.
 */
struct tnek_user_msg {
	char tag[32];			/* 0x00, node tag + 2 pad */
	u64 api;			/* 0x20 */
	int pid;			/* 0x28 */
	u8 data[0x3e8 - 0x2c];		/* 0x2c, first 256 bytes used */
	char name[32];			/* 0x3e8 */
};

/*
 * Private node layout, measured from the .ko: list at +0x00, tag at +0x10,
 * api at +0x30, type at +0x38, data at +0x3c, name at +0x3f8, size 0x418.
 */
struct tnek_api_list_node {
	struct list_head list;		/* 0x000 */
	char tag[TNEK_TAG_SIZE];	/* 0x010 */
	u16 pad;			/* 0x02e */
	u64 api;			/* 0x030 */
	int type;			/* 0x038 */
	char data[TNEK_DATA_SIZE];	/* 0x03c */
	u8 reserved[0x3f8 - 0x13c];	/* 0x13c */
	char name[TNEK_NAME_SIZE];	/* 0x3f8 */
};

_Static_assert(offsetof(struct tnek_api_list_node, api) == 0x30,
	       "tnek api must stay at +0x30");
_Static_assert(offsetof(struct tnek_api_list_node, type) == 0x38,
	       "tnek type must stay at +0x38");
_Static_assert(offsetof(struct tnek_api_list_node, data) == 0x3c,
	       "tnek data must stay at +0x3c");
_Static_assert(offsetof(struct tnek_api_list_node, name) == 0x3f8,
	       "tnek name must stay at +0x3f8");
_Static_assert(sizeof(struct tnek_api_list_node) == TNEK_NODE_SIZE,
	       "tnek node must be 0x418 bytes");
_Static_assert(sizeof(struct tnek_user_msg) == TNEK_MSG_SIZE,
	       "tnek user message must be 0x408 bytes");

extern wait_queue_head_t tne_pollwait;
extern wait_queue_head_t tne_readwait;

struct tnek_api_list_node *tnek_create_node(const char *tag, u64 api,
					    int type, const char *name,
					    const char *data);
void tnek_free_node(struct tnek_api_list_node *node);
struct tnek_api_list_node *tnek_get_node_from_list(void);
int tnek_get_tne_queue_len(void);
void tnek_wake_up_poll_and_read(void);
int tnek_api_init(void);
void tnek_api_exit(void);

void tnek_caller_api(const char *tag, u64 api, int type, const char *data);
void simp_tnek_caller_api(const char *tag, int type);
void simp_multipid_tnek_caller_api(const char *tag, const char *name);
void multipid_tnek_caller_api(const char *tag, u64 api, const char *name,
			      const char *data);

#endif /* __TNEK_H__ */
