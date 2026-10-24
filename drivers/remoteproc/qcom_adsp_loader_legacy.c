// SPDX-License-Identifier: GPL-2.0-only
/*
 * /sys/kernel/boot_adsp for the stock lahaina DT's qcom,adsp-loader node.
 *
 * The 5.4 adsp-loader booted the ADSP when init writes 1 to
 * /sys/kernel/boot_adsp/boot (init.qti.kernel.rc, early-boot). The 6.1
 * audio-kernel adsp-loader needs a qcom,rproc-handle and the audio SPF
 * stack; the stock DT node has neither, so this finds the ADSP rproc by
 * its 5.4 PIL node instead.
 */

#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>

struct adsp_loader {
	struct device *dev;
	struct rproc *rproc;
	struct kobject *kobj;
	struct work_struct boot_work;
	bool booted;
};

static struct adsp_loader *loader;

static void adsp_loader_boot_work(struct work_struct *work)
{
	struct adsp_loader *ldr = container_of(work, struct adsp_loader,
					       boot_work);
	int ret;

	if (ldr->booted)
		return;

	ret = rproc_boot(ldr->rproc);
	if (ret) {
		dev_err(ldr->dev, "ADSP boot failed: %d\n", ret);
		return;
	}

	ldr->booted = true;
	dev_info(ldr->dev, "ADSP image is loaded\n");
}

static ssize_t boot_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t count)
{
	int boot, ret;

	ret = kstrtoint(buf, 0, &boot);
	if (ret)
		return ret;

	if (boot == 1)
		schedule_work(&loader->boot_work);
	else
		dev_info(loader->dev, "ignoring boot_adsp/boot %d\n", boot);

	return count;
}

static struct kobj_attribute boot_attr = __ATTR_WO(boot);

static struct attribute *adsp_loader_attrs[] = {
	&boot_attr.attr,
	NULL,
};

static const struct attribute_group adsp_loader_group = {
	.attrs = adsp_loader_attrs,
};

static int adsp_loader_probe(struct platform_device *pdev)
{
	struct adsp_loader *ldr;
	int ret;

	if (loader)
		return -EBUSY;

	ldr = devm_kzalloc(&pdev->dev, sizeof(*ldr), GFP_KERNEL);
	if (!ldr)
		return -ENOMEM;

	ldr->rproc = qcom_rproc_get_pil_tz_generic("adsp");
	if (!ldr->rproc)
		return -EPROBE_DEFER;

	ldr->dev = &pdev->dev;
	INIT_WORK(&ldr->boot_work, adsp_loader_boot_work);

	ldr->kobj = kobject_create_and_add("boot_adsp", kernel_kobj);
	if (!ldr->kobj) {
		ret = -ENOMEM;
		goto put_rproc;
	}

	ret = sysfs_create_group(ldr->kobj, &adsp_loader_group);
	if (ret)
		goto put_kobj;

	loader = ldr;
	platform_set_drvdata(pdev, ldr);

	return 0;

put_kobj:
	kobject_put(ldr->kobj);
put_rproc:
	rproc_put(ldr->rproc);
	return ret;
}

static int adsp_loader_remove(struct platform_device *pdev)
{
	struct adsp_loader *ldr = platform_get_drvdata(pdev);

	sysfs_remove_group(ldr->kobj, &adsp_loader_group);
	kobject_put(ldr->kobj);
	cancel_work_sync(&ldr->boot_work);
	if (ldr->booted)
		rproc_shutdown(ldr->rproc);
	rproc_put(ldr->rproc);
	loader = NULL;

	return 0;
}

static const struct of_device_id adsp_loader_match[] = {
	{ .compatible = "qcom,adsp-loader" },
	{ }
};
MODULE_DEVICE_TABLE(of, adsp_loader_match);

static struct platform_driver adsp_loader_driver = {
	.probe = adsp_loader_probe,
	.remove = adsp_loader_remove,
	.driver = {
		.name = "qcom-adsp-loader-legacy",
		.of_match_table = adsp_loader_match,
	},
};
module_platform_driver(adsp_loader_driver);

MODULE_DESCRIPTION("ADSP loader for the 5.4 qcom,adsp-loader binding");
MODULE_LICENSE("GPL");
