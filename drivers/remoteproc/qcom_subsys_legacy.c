// SPDX-License-Identifier: GPL-2.0-only
/*
 * The 5.4 msm_subsys interface for the stock lahaina userspace.
 *
 * On 5.4 the peripheral manager (pm-service, libmdmdetect) finds the
 * subsystems under /sys/bus/msm_subsys/devices/subsys<N>/{name,state} and
 * keeps one running by holding /dev/subsys_<name> open: the first open
 * boots it, the last close shuts it down (subsystem_restart.c). 6.1 has
 * remoteproc instead, so this provides the same bus, attributes and char
 * devices for the rprocs of the 5.4 PIL nodes (qcom,pil-tz-generic) that
 * userspace starts this way. The devices sit under the PIL platform device,
 * as on 5.4, so the vendor file_contexts labels still apply.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

/* qcom,firmware-name of the PIL nodes that userspace boots through here */
static const char * const subsys_names[] = { "modem" };

/* how long to wait for the PAS driver to probe the rprocs */
#define SUBSYS_RETRY_MS		500
#define SUBSYS_RETRIES		120

struct subsys_legacy {
	struct device dev;
	struct cdev cdev;
	struct device *chrdev;
	struct rproc *rproc;
	const char *name;
	struct mutex lock;
	unsigned int users;
	int id;
	bool related;
};

static struct bus_type msm_subsys_bus = {
	.name = "msm_subsys",
};

static struct class *subsys_class;
static dev_t subsys_devt;
static DEFINE_IDA(subsys_ida);

#define to_subsys(d) container_of(d, struct subsys_legacy, dev)

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	return sysfs_emit(buf, "%s\n", to_subsys(dev)->name);
}
static DEVICE_ATTR_RO(name);

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct rproc *rproc = to_subsys(dev)->rproc;

	switch (rproc->state) {
	case RPROC_RUNNING:
	case RPROC_ATTACHED:
		return sysfs_emit(buf, "ONLINE\n");
	case RPROC_CRASHED:
		return sysfs_emit(buf, "CRASHED\n");
	default:
		return sysfs_emit(buf, "OFFLINE\n");
	}
}
static DEVICE_ATTR_RO(state);

static ssize_t firmware_name_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", to_subsys(dev)->rproc->firmware);
}
static DEVICE_ATTR_RO(firmware_name);

static ssize_t crash_count_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "0\n");
}
static DEVICE_ATTR_RO(crash_count);

/*
 * SYSTEM: a crash takes the whole system down on 5.4; here it leaves the
 * rproc crashed (recovery disabled). RELATED: restart only the subsystem.
 */
static ssize_t restart_level_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n",
			  to_subsys(dev)->related ? "RELATED" : "SYSTEM");
}

static ssize_t restart_level_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct subsys_legacy *subsys = to_subsys(dev);

	if (sysfs_streq(buf, "RELATED"))
		subsys->related = true;
	else if (sysfs_streq(buf, "SYSTEM"))
		subsys->related = false;
	else
		return -EINVAL;

	subsys->rproc->recovery_disabled = !subsys->related;
	dev_info(dev, "%s: restart level %s\n", subsys->name,
		 subsys->related ? "RELATED" : "SYSTEM");
	return count;
}
static DEVICE_ATTR_RW(restart_level);

static struct attribute *subsys_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_state.attr,
	&dev_attr_firmware_name.attr,
	&dev_attr_crash_count.attr,
	&dev_attr_restart_level.attr,
	NULL,
};
ATTRIBUTE_GROUPS(subsys);

static int subsys_open(struct inode *inode, struct file *file)
{
	struct subsys_legacy *subsys =
		container_of(inode->i_cdev, struct subsys_legacy, cdev);
	int ret = 0;

	mutex_lock(&subsys->lock);
	if (!subsys->users) {
		dev_info(&subsys->dev, "%s: boot requested by %s[%d]\n",
			 subsys->name, current->comm, task_pid_nr(current));
		ret = rproc_boot(subsys->rproc);
		if (ret)
			dev_err(&subsys->dev, "%s: boot failed: %d\n",
				subsys->name, ret);
	}
	if (!ret)
		subsys->users++;
	mutex_unlock(&subsys->lock);

	file->private_data = subsys;
	return ret;
}

static int subsys_release(struct inode *inode, struct file *file)
{
	struct subsys_legacy *subsys = file->private_data;

	mutex_lock(&subsys->lock);
	if (!--subsys->users) {
		dev_info(&subsys->dev, "%s: shutdown, last user %s[%d]\n",
			 subsys->name, current->comm, task_pid_nr(current));
		rproc_shutdown(subsys->rproc);
	}
	mutex_unlock(&subsys->lock);
	return 0;
}

static const struct file_operations subsys_fops = {
	.owner = THIS_MODULE,
	.open = subsys_open,
	.release = subsys_release,
	.llseek = noop_llseek,
};

static void subsys_dev_release(struct device *dev)
{
	struct subsys_legacy *subsys = to_subsys(dev);

	ida_free(&subsys_ida, subsys->id);
	rproc_put(subsys->rproc);
	kfree(subsys);
}

static int subsys_add(const char *name, struct rproc *rproc)
{
	struct subsys_legacy *subsys;
	int id, ret;

	id = ida_alloc_max(&subsys_ida, ARRAY_SIZE(subsys_names) - 1,
			   GFP_KERNEL);
	if (id < 0)
		return id;

	subsys = kzalloc(sizeof(*subsys), GFP_KERNEL);
	if (!subsys) {
		ida_free(&subsys_ida, id);
		return -ENOMEM;
	}

	subsys->id = id;
	subsys->name = name;
	subsys->rproc = rproc;
	mutex_init(&subsys->lock);
	/* as rproc_alloc() leaves it unless userspace asks for RELATED */
	subsys->related = !rproc->recovery_disabled;

	device_initialize(&subsys->dev);
	subsys->dev.bus = &msm_subsys_bus;
	subsys->dev.parent = rproc->dev.parent;
	subsys->dev.groups = subsys_groups;
	subsys->dev.release = subsys_dev_release;
	dev_set_name(&subsys->dev, "subsys%d", id);

	cdev_init(&subsys->cdev, &subsys_fops);
	subsys->cdev.owner = THIS_MODULE;

	/* no dev_t here: the node is the class device, /dev/subsys_<name> */
	ret = device_add(&subsys->dev);
	if (ret)
		goto err_put;

	ret = cdev_add(&subsys->cdev, subsys_devt + id, 1);
	if (ret)
		goto err_del;

	subsys->chrdev = device_create(subsys_class, &subsys->dev,
				       subsys_devt + id, subsys, "subsys_%s",
				       name);
	if (IS_ERR(subsys->chrdev)) {
		ret = PTR_ERR(subsys->chrdev);
		goto err_cdev;
	}

	dev_info(&subsys->dev, "%s: /dev/subsys_%s for %s\n", name, name,
		 dev_name(rproc->dev.parent));
	return 0;

err_cdev:
	cdev_del(&subsys->cdev);
err_del:
	device_del(&subsys->dev);
err_put:
	put_device(&subsys->dev);
	return ret;
}

static unsigned long subsys_pending = GENMASK(ARRAY_SIZE(subsys_names) - 1, 0);
static unsigned int subsys_tries;

static void subsys_probe_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(subsys_work, subsys_probe_work);

static void subsys_probe_work(struct work_struct *work)
{
	struct rproc *rproc;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(subsys_names); i++) {
		if (!test_bit(i, &subsys_pending))
			continue;

		rproc = qcom_rproc_get_pil_tz_generic(subsys_names[i]);
		if (!rproc)
			continue;

		ret = subsys_add(subsys_names[i], rproc);
		if (ret)
			pr_err("subsys_legacy: %s: %d\n", subsys_names[i], ret);
		clear_bit(i, &subsys_pending);
	}

	if (subsys_pending && ++subsys_tries < SUBSYS_RETRIES)
		schedule_delayed_work(&subsys_work,
				      msecs_to_jiffies(SUBSYS_RETRY_MS));
	else if (subsys_pending)
		pr_err("subsys_legacy: no rproc for %#lx\n", subsys_pending);
}

static int __init subsys_legacy_init(void)
{
	int ret;

	ret = bus_register(&msm_subsys_bus);
	if (ret)
		return ret;

	ret = alloc_chrdev_region(&subsys_devt, 0, ARRAY_SIZE(subsys_names),
				  "subsys");
	if (ret)
		goto err_bus;

	subsys_class = class_create(THIS_MODULE, "subsys");
	if (IS_ERR(subsys_class)) {
		ret = PTR_ERR(subsys_class);
		goto err_region;
	}

	schedule_delayed_work(&subsys_work, 0);
	return 0;

err_region:
	unregister_chrdev_region(subsys_devt, ARRAY_SIZE(subsys_names));
err_bus:
	bus_unregister(&msm_subsys_bus);
	return ret;
}
late_initcall(subsys_legacy_init);

MODULE_DESCRIPTION("5.4 msm_subsys interface on remoteproc for lahaina");
MODULE_LICENSE("GPL");
