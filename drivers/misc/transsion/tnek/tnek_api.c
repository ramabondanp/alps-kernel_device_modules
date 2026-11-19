// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Transsion Inc.
 *
 * Transsion TNE kernel queue, reconstructed from the X6871 tnek.ko.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/wait.h>

#include "tnek.h"

static LIST_HEAD(tnek_api_list);
static DEFINE_SPINLOCK(tnek_api_lock);
static int tnek_api_list_len;

DECLARE_WAIT_QUEUE_HEAD(tne_pollwait);
DECLARE_WAIT_QUEUE_HEAD(tne_readwait);
EXPORT_SYMBOL(tne_pollwait);
EXPORT_SYMBOL(tne_readwait);

/*
 * 6.12 removed strlcpy; the vendor module carries and exports its own copy,
 * so keep the same ABI for callers that still use it.
 */
size_t strlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t len = (ret >= size) ? size - 1 : ret;

		memcpy(dest, src, len);
		dest[len] = '\0';
	}

	return ret;
}
EXPORT_SYMBOL(strlcpy);

struct tnek_api_list_node *tnek_create_node(const char *tag, u64 api,
					    int type, const char *name,
					    const char *data)
{
	struct tnek_api_list_node *node;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node) {
		pr_err("tnek: %s, kzalloc fail,return.", __func__);
		return NULL;
	}

	node->api = api;
	node->type = type;

	if (tag)
		strlcpy(node->tag, tag, sizeof(node->tag));
	else
		strlcpy(node->tag, "K_Unknown", sizeof(node->tag));

	if (name)
		strlcpy(node->name, name, sizeof(node->name));

	if (data)
		strlcpy(node->data, data, sizeof(node->data));

	pr_info("tnek: %s, type:%lld, tag:%s.\n", __func__,
		(long long)api, node->tag);

	return node;
}
EXPORT_SYMBOL(tnek_create_node);

void tnek_free_node(struct tnek_api_list_node *node)
{
	if (node)
		kfree(node);
}
EXPORT_SYMBOL(tnek_free_node);

struct tnek_api_list_node *tnek_get_node_from_list(void)
{
	struct tnek_api_list_node *node = NULL;
	unsigned long flags;

	spin_lock_irqsave(&tnek_api_lock, flags);
	if (tnek_api_list_len >= 1) {
		if (list_empty(&tnek_api_list)) {
			pr_info("tnek: %s, Invalid tnek_api_list_node struct\n",
				__func__);
		} else {
			node = list_first_entry(&tnek_api_list,
						struct tnek_api_list_node,
						list);
			list_del(&node->list);
			tnek_api_list_len--;
		}
	}
	spin_unlock_irqrestore(&tnek_api_lock, flags);

	return node;
}
EXPORT_SYMBOL(tnek_get_node_from_list);

int tnek_get_tne_queue_len(void)
{
	unsigned long flags;
	int len;

	spin_lock_irqsave(&tnek_api_lock, flags);
	len = tnek_api_list_len;
	spin_unlock_irqrestore(&tnek_api_lock, flags);

	return len;
}
EXPORT_SYMBOL(tnek_get_tne_queue_len);

static void tnek_queue_request(struct tnek_api_list_node *node)
{
	struct tnek_api_list_node *first;
	unsigned long flags;

	if (!node)
		return;

	spin_lock_irqsave(&tnek_api_lock, flags);

	while (tnek_api_list_len >= TNEK_MAX_QUEUE) {
		pr_info("tnek: %s ,Current list is longer than %d, native tne may be blocked!!! skip first.\n",
			__func__, TNEK_MAX_QUEUE);
		if (list_empty(&tnek_api_list))
			break;
		first = list_first_entry(&tnek_api_list,
					 struct tnek_api_list_node, list);
		list_del(&first->list);
		kfree(first);
		tnek_api_list_len--;
	}

	list_add_tail(&node->list, &tnek_api_list);
	tnek_api_list_len++;

	spin_unlock_irqrestore(&tnek_api_lock, flags);
}

void tnek_wake_up_poll_and_read(void)
{
	wake_up_interruptible(&tne_pollwait);
	wake_up_interruptible(&tne_readwait);
}
EXPORT_SYMBOL(tnek_wake_up_poll_and_read);

void tnek_caller_api(const char *tag, u64 api, int type, const char *data)
{
	char name[TNEK_NAME_SIZE] = {0};
	struct tnek_api_list_node *node;

	node = tnek_create_node(tag, api, type, name, data);
	if (!node)
		return;

	tnek_queue_request(node);
	tnek_wake_up_poll_and_read();
}
EXPORT_SYMBOL(tnek_caller_api);

void simp_tnek_caller_api(const char *tag, int type)
{
	char name[TNEK_NAME_SIZE] = {0};
	char data[TNEK_DATA_SIZE] = {0};
	struct tnek_api_list_node *node;

	node = tnek_create_node(tag, 0, type, name, data);
	if (!node)
		return;

	tnek_queue_request(node);
	tnek_wake_up_poll_and_read();
}
EXPORT_SYMBOL(simp_tnek_caller_api);

void simp_multipid_tnek_caller_api(const char *tag, const char *name)
{
	char data[TNEK_DATA_SIZE] = {0};
	struct tnek_api_list_node *node;

	node = tnek_create_node(tag, 0, 0, name, data);
	if (!node)
		return;

	tnek_queue_request(node);
	tnek_wake_up_poll_and_read();
}
EXPORT_SYMBOL(simp_multipid_tnek_caller_api);

void multipid_tnek_caller_api(const char *tag, u64 api, const char *name,
			      const char *data)
{
	struct tnek_api_list_node *node;

	node = tnek_create_node(tag, api, 0, name, data);
	if (!node)
		return;

	tnek_queue_request(node);
	tnek_wake_up_poll_and_read();
}
EXPORT_SYMBOL(multipid_tnek_caller_api);

int tnek_api_init(void)
{
	INIT_LIST_HEAD(&tnek_api_list);
	tnek_api_list_len = 0;
	return 0;
}
EXPORT_SYMBOL(tnek_api_init);

void tnek_api_exit(void)
{
}
EXPORT_SYMBOL(tnek_api_exit);
