// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2014  Google, Inc.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include "internal.h"

static DEFINE_MUTEX(pmsg_lock);

/*
 * DEBUG (bring-up): "k61.pmsg_drop=<comm>,..." silently drops the pmsg
 * records of processes with those names, so a noisy daemon can't fill the
 * pmsg ramoops zone and push out the logs of interest. On user builds only
 * logd writes pmsg, for every client, so the record header's pid names the
 * process, not current. Each record arrives as several writes (one per
 * writev segment); once a header matches, the rest of the record is
 * dropped too.
 */
static char k61_pmsg_drop[64];
static size_t k61_pmsg_skip;	/* rest of a dropped record; pmsg_lock */

struct k61_pmsg_header {	/* android_pmsg_log_header_t */
	u8 magic;		/* 'l' */
	__le16 len;		/* whole record, this header included */
	__le16 uid;
	__le16 pid;
} __packed;

static int __init k61_pmsg_drop_setup(char *str)
{
	strscpy(k61_pmsg_drop, str, sizeof(k61_pmsg_drop));
	return 1;
}
__setup("k61.pmsg_drop=", k61_pmsg_drop_setup);

static bool k61_pmsg_drop_listed(const char *comm)
{
	const char *p = k61_pmsg_drop;
	size_t len;

	while (*p) {
		len = strcspn(p, ",");
		if (len && len == strlen(comm) && !strncmp(p, comm, len))
			return true;
		p += len;
		if (*p)
			p++;
	}
	return false;
}

/* Called under pmsg_lock for every write */
static bool k61_pmsg_dropped(const char __user *buf, size_t count)
{
	struct k61_pmsg_header hdr;
	char comm[TASK_COMM_LEN];
	struct task_struct *task;
	bool found = false;
	size_t len;

	if (k61_pmsg_skip) {
		k61_pmsg_skip -= min(count, k61_pmsg_skip);
		return true;
	}
	if (count < sizeof(hdr) || copy_from_user(&hdr, buf, sizeof(hdr)) ||
	    hdr.magic != 'l')
		return false;

	rcu_read_lock();
	task = find_task_by_vpid(le16_to_cpu(hdr.pid));
	if (task) {
		get_task_comm(comm, task);
		found = true;
	}
	rcu_read_unlock();
	if (!found || !k61_pmsg_drop_listed(comm))
		return false;

	len = le16_to_cpu(hdr.len);
	k61_pmsg_skip = len > count ? len - count : 0;
	return true;
}

static ssize_t write_pmsg(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct pstore_record record;
	int ret;

	if (!count)
		return 0;

	pstore_record_init(&record, psinfo);
	record.type = PSTORE_TYPE_PMSG;
	record.size = count;

	/* check outside lock, page in any data. write_user also checks */
	if (!access_ok(buf, count))
		return -EFAULT;

	mutex_lock(&pmsg_lock);
	if (k61_pmsg_drop[0] && k61_pmsg_dropped(buf, count)) {
		mutex_unlock(&pmsg_lock);
		return count;
	}
	ret = psinfo->write_user(&record, buf);
	mutex_unlock(&pmsg_lock);
	return ret ? ret : count;
}

static const struct file_operations pmsg_fops = {
	.owner		= THIS_MODULE,
	.llseek		= noop_llseek,
	.write		= write_pmsg,
};

static struct class *pmsg_class;
static int pmsg_major;
#define PMSG_NAME "pmsg"
#undef pr_fmt
#define pr_fmt(fmt) PMSG_NAME ": " fmt

static char *pmsg_devnode(struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0220;
	return NULL;
}

void pstore_register_pmsg(void)
{
	struct device *pmsg_device;

	pmsg_major = register_chrdev(0, PMSG_NAME, &pmsg_fops);
	if (pmsg_major < 0) {
		pr_err("register_chrdev failed\n");
		goto err;
	}

	pmsg_class = class_create(THIS_MODULE, PMSG_NAME);
	if (IS_ERR(pmsg_class)) {
		pr_err("device class file already in use\n");
		goto err_class;
	}
	pmsg_class->devnode = pmsg_devnode;

	pmsg_device = device_create(pmsg_class, NULL, MKDEV(pmsg_major, 0),
					NULL, "%s%d", PMSG_NAME, 0);
	if (IS_ERR(pmsg_device)) {
		pr_err("failed to create device\n");
		goto err_device;
	}
	return;

err_device:
	class_destroy(pmsg_class);
err_class:
	unregister_chrdev(pmsg_major, PMSG_NAME);
err:
	return;
}

void pstore_unregister_pmsg(void)
{
	device_destroy(pmsg_class, MKDEV(pmsg_major, 0));
	class_destroy(pmsg_class);
	unregister_chrdev(pmsg_major, PMSG_NAME);
}
