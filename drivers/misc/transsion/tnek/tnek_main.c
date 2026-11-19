// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Transsion Inc.
 *
 * Transsion TNE kernel queue misc device, reconstructed from tnek.ko.
 */

#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "tnek.h"

static int tnek_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int tnek_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long tnek_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static ssize_t tnek_read(struct file *file, char __user *buf, size_t count,
			 loff_t *ppos)
{
	struct tnek_api_list_node *node;
	bool nonblock = file->f_flags & O_NONBLOCK;
	int ret;

	if (count < TNEK_MSG_SIZE) {
		pr_info("tnek: %s, readbuf is too small,just quit.\n",
			__func__);
		return -ENOMEM;
	}

	for (;;) {
		node = tnek_get_node_from_list();
		if (node)
			break;
		if (nonblock)
			return -EAGAIN;

		ret = wait_event_interruptible(tne_readwait,
				tnek_get_tne_queue_len() > 0);
		if (ret)
			return ret;
	}

	ret = copy_to_user(buf, (u8 *)node + 0x10, TNEK_MSG_SIZE);
	if (ret) {
		pr_info("tnek: %s, copy_to_user return %d.\n", __func__, ret);
		ret = -EFAULT;
	} else {
		ret = TNEK_MSG_SIZE;
	}

	pr_info("tnek: %s, tag=%s, type=%lld, pid=%d, ret=%d\n", __func__,
		node->tag, (long long)node->api, node->type, TNEK_MSG_SIZE);

	tnek_free_node(node);

	return ret;
}

static ssize_t tnek_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct tnek_user_msg *msg;
	size_t left;

	if (count != TNEK_MSG_SIZE) {
		pr_err("k-5.10 tnek_write count illegality\n");
		return -1;
	}

	msg = kmalloc(TNEK_MSG_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	left = copy_from_user(msg, buf, TNEK_MSG_SIZE);
	if (left) {
		memset((u8 *)msg + (TNEK_MSG_SIZE - left), 0, left);
		kfree(msg);
		return -EFAULT;
	}

	pr_info("tnek:tnek_write:tag=%s,api->pid=%d\n", msg->tag, msg->pid);

	tnek_caller_api(msg->tag, msg->api, msg->pid, msg->data);

	kfree(msg);

	/* the vendor module returns 0 here, not the write count */
	return 0;
}

static __poll_t tnek_poll(struct file *file, poll_table *wait)
{
	if (tnek_get_tne_queue_len() > 0)
		return EPOLLIN | EPOLLRDNORM;

	poll_wait(file, &tne_pollwait, wait);

	return 0;
}

static const struct file_operations tnek_fops = {
	.owner		= THIS_MODULE,
	.open		= tnek_open,
	.release	= tnek_release,
	.read		= tnek_read,
	.write		= tnek_write,
	.poll		= tnek_poll,
	.unlocked_ioctl	= tnek_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= tnek_ioctl,
#endif
};

static struct miscdevice tnek_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "tnek",
	.fops	= &tnek_fops,
};

static int __init tnek_init(void)
{
	int ret;

	init_waitqueue_head(&tne_pollwait);
	init_waitqueue_head(&tne_readwait);

	tnek_api_init();

	ret = misc_register(&tnek_misc);
	if (ret)
		pr_err("tnek: misc_register failed!\n");
	else
		pr_info("tnek: misc_register success!\n");

	return ret;
}

static void __exit tnek_exit(void)
{
	misc_deregister(&tnek_misc);
	tnek_api_exit();
}

module_init(tnek_init);
module_exit(tnek_exit);

MODULE_LICENSE("GPL");
