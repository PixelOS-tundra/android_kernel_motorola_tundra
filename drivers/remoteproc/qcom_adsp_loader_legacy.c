// SPDX-License-Identifier: GPL-2.0-only
/*
 * /sys/kernel/boot_{adsp,cdsp,slpi} for the stock lahaina DT's loader nodes.
 *
 * On 5.4 the vendor init boots the DSPs by writing 1 to
 * /sys/kernel/boot_<name>/boot (init.qti.kernel.rc, early-boot), served by
 * the adsp-loader (qcom,adsp-loader), cdsp-loader (qcom,cdsp-loader) and
 * sensors_ssc (qcom,msm-ssc-sensors) drivers. The 6.1 loaders need a
 * qcom,rproc-handle (and, for the ADSP, the audio SPF stack); the stock DT
 * nodes have neither, so this finds the rproc by its 5.4 PIL node instead.
 */

#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>

struct dsp_loader_data {
	/* sysfs directory under /sys/kernel and default PIL firmware name */
	const char *name;
	/* DT property naming the PIL node's qcom,firmware-name, if any */
	const char *fw_prop;
	/* also provide the 5.4 sensors_ssc "ssr" file (write 1: restart) */
	bool ssr;
};

struct dsp_loader {
	struct device *dev;
	const char *name;
	const char *fw_name;
	struct rproc *rproc;
	struct kobject *kobj;
	struct kobj_attribute boot_attr;
	struct kobj_attribute ssr_attr;
	struct attribute *attrs[3];
	struct attribute_group group;
	struct work_struct boot_work;
	bool booted;
};

/* DEBUG: k61.dsp_skip=<name>[,<name>...] ignores boot_<name>/boot writes */
static char k61_dsp_skip[64];

static int __init k61_dsp_skip_setup(char *s)
{
	strscpy(k61_dsp_skip, s, sizeof(k61_dsp_skip));
	return 1;
}
__setup("k61.dsp_skip=", k61_dsp_skip_setup);

static bool k61_dsp_skipped(const char *name)
{
	const char *p = k61_dsp_skip;
	size_t len = strlen(name);

	while (*p) {
		if (!strncmp(p, name, len) && (p[len] == ',' || !p[len]))
			return true;
		p = strchrnul(p, ',');
		if (*p)
			p++;
	}
	return false;
}

static void dsp_loader_boot_work(struct work_struct *work)
{
	struct dsp_loader *ldr = container_of(work, struct dsp_loader,
					      boot_work);
	int ret;

	if (ldr->booted)
		return;

	ret = rproc_boot(ldr->rproc);
	if (ret) {
		dev_err(ldr->dev, "%s boot failed: %d\n", ldr->fw_name, ret);
		return;
	}

	ldr->booted = true;
	dev_info(ldr->dev, "%s image is loaded\n", ldr->fw_name);
}

static ssize_t boot_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t count)
{
	struct dsp_loader *ldr = container_of(attr, struct dsp_loader,
					      boot_attr);
	int boot, ret;

	ret = kstrtoint(buf, 0, &boot);
	if (ret)
		return ret;

	if (boot == 1 && k61_dsp_skipped(ldr->name))
		dev_info(ldr->dev, "k61.dsp_skip: not booting %s\n", ldr->name);
	else if (boot == 1)
		schedule_work(&ldr->boot_work);
	else
		dev_info(ldr->dev, "ignoring boot_%s/boot %d\n", ldr->name,
			 boot);

	return count;
}

static ssize_t ssr_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	struct dsp_loader *ldr = container_of(attr, struct dsp_loader,
					      ssr_attr);
	int cmd;

	if (kstrtoint(buf, 10, &cmd) || cmd != 1)
		return -EINVAL;

	if (!ldr->booted)
		return -EINVAL;

	dev_err(ldr->dev, "Something went wrong with %s, restarting\n",
		ldr->fw_name);
	rproc_report_crash(ldr->rproc, RPROC_FATAL_ERROR);

	return count;
}

static int dsp_loader_probe(struct platform_device *pdev)
{
	const struct dsp_loader_data *data;
	struct dsp_loader *ldr;
	char dir[16];
	int ret;

	data = of_device_get_match_data(&pdev->dev);
	if (!data)
		return -EINVAL;

	ldr = devm_kzalloc(&pdev->dev, sizeof(*ldr), GFP_KERNEL);
	if (!ldr)
		return -ENOMEM;

	ldr->name = data->name;
	ldr->fw_name = data->name;
	if (data->fw_prop)
		of_property_read_string(pdev->dev.of_node, data->fw_prop,
					&ldr->fw_name);

	ldr->rproc = qcom_rproc_get_pil_tz_generic(ldr->fw_name);
	if (!ldr->rproc)
		return -EPROBE_DEFER;

	ldr->dev = &pdev->dev;
	INIT_WORK(&ldr->boot_work, dsp_loader_boot_work);

	sysfs_attr_init(&ldr->boot_attr.attr);
	ldr->boot_attr.attr.name = "boot";
	ldr->boot_attr.attr.mode = 0220;
	ldr->boot_attr.store = boot_store;
	ldr->attrs[0] = &ldr->boot_attr.attr;
	if (data->ssr) {
		sysfs_attr_init(&ldr->ssr_attr.attr);
		ldr->ssr_attr.attr.name = "ssr";
		ldr->ssr_attr.attr.mode = 0220;
		ldr->ssr_attr.store = ssr_store;
		ldr->attrs[1] = &ldr->ssr_attr.attr;
	}
	ldr->group.attrs = ldr->attrs;

	snprintf(dir, sizeof(dir), "boot_%s", ldr->name);
	ldr->kobj = kobject_create_and_add(dir, kernel_kobj);
	if (!ldr->kobj) {
		ret = -ENOMEM;
		goto put_rproc;
	}

	ret = sysfs_create_group(ldr->kobj, &ldr->group);
	if (ret)
		goto put_kobj;

	platform_set_drvdata(pdev, ldr);

	return 0;

put_kobj:
	kobject_put(ldr->kobj);
put_rproc:
	rproc_put(ldr->rproc);
	return ret;
}

static int dsp_loader_remove(struct platform_device *pdev)
{
	struct dsp_loader *ldr = platform_get_drvdata(pdev);

	sysfs_remove_group(ldr->kobj, &ldr->group);
	kobject_put(ldr->kobj);
	cancel_work_sync(&ldr->boot_work);
	if (ldr->booted)
		rproc_shutdown(ldr->rproc);
	rproc_put(ldr->rproc);

	return 0;
}

static const struct dsp_loader_data adsp_loader_data = {
	.name = "adsp",
};

/* qcom,proc-img-to-load = "cdsp" */
static const struct dsp_loader_data cdsp_loader_data = {
	.name = "cdsp",
	.fw_prop = "qcom,proc-img-to-load",
};

/* qcom,firmware-name = "slpi" */
static const struct dsp_loader_data slpi_loader_data = {
	.name = "slpi",
	.fw_prop = "qcom,firmware-name",
	.ssr = true,
};

static const struct of_device_id dsp_loader_match[] = {
	{ .compatible = "qcom,adsp-loader", .data = &adsp_loader_data },
	{ .compatible = "qcom,cdsp-loader", .data = &cdsp_loader_data },
	{ .compatible = "qcom,msm-ssc-sensors", .data = &slpi_loader_data },
	{ }
};
MODULE_DEVICE_TABLE(of, dsp_loader_match);

static struct platform_driver dsp_loader_driver = {
	.probe = dsp_loader_probe,
	.remove = dsp_loader_remove,
	.driver = {
		.name = "qcom-adsp-loader-legacy",
		.of_match_table = dsp_loader_match,
	},
};
module_platform_driver(dsp_loader_driver);

MODULE_DESCRIPTION("DSP loaders for the 5.4 lahaina loader bindings");
MODULE_LICENSE("GPL");
