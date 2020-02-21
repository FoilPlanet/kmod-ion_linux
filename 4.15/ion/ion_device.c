/*
 *
 * Copyright (C) 2020 FoilPlanet, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sizes.h>

#include "ion.h"
#include "ion_priv.h"

static struct ion_device *ion_dev = NULL;
extern struct file       *shared_file;

/*-------------------------- ion-share device --------------------------------*/

struct ion_share_device {
	struct miscdevice misc;
};

static long ion_share_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	if (cmd == ION_IOC_CUSTOM) { // ION_IOC_SHARE_FD
		// already in share mode
		pr_notice("ion-shared: ignore ioctl %08x\n", (uint32_t)filp);
		return 0;
	}

	pr_debug("ion-shared: ioctl %08x\n", cmd);

	if (shared_file && ion_dev) {
		// share_id = file->private_data;
		return ion_dev->dev.fops->unlocked_ioctl(shared_file, cmd, arg);
	}
	// return ion_dev->dev.fops->unlocked_ioctl(filp, cmd, arg);
	return -ENOTTY;
}

static int ion_share_open(struct inode *inode, struct file *file)
{
	pr_debug("ion-shared: open %08x\n", (uint32_t)file);
	// return ion_dev->dev.fops->open(inode, file);
	// file->private_data = share_id;
	return 0;
}

static int ion_share_release(struct inode *inode, struct file *file)
{
	pr_debug("ion-shared: close %08x\n", (uint32_t)file);
	// return ion_dev->dev.fops->release(inode, file);
	// share_id = file->private_data;
	return 0;
}

static const struct file_operations ion_share_fops = {
	.owner          = THIS_MODULE,
	.open           = ion_share_open,
	.release        = ion_share_release,
	.unlocked_ioctl = ion_share_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = ion_share_ioctl,
#endif
};

static struct ion_share_device *
_ion_share_device_create(struct platform_device *pdev)
{
	int ret;
	struct ion_share_device *hydev;
	struct miscdevice 	    *pmisc;

	hydev = devm_kzalloc(&pdev->dev, sizeof(*hydev), GFP_KERNEL);
	if (!hydev)
		return -ENOMEM;

	pmisc = &hydev->misc;

	pmisc->minor  = MISC_DYNAMIC_MINOR;
	pmisc->name   = "ion-share";
	pmisc->fops   = &ion_share_fops;
	pmisc->parent = &pdev->dev;
	if (0 != (ret = misc_register(pmisc))) {
		pr_err("failed to register misc device.\n");
	}

	platform_set_drvdata(pdev, hydev);
	return ret;
}

static int
_ion_share_device_remove(struct platform_device *pdev)
{
	struct ion_share_device *hydev = platform_get_drvdata(pdev);
	if (!hydev)
		return -ENODATA;

	misc_deregister(&hydev->misc);
	return 0;
}

/*------------------------- ion device resources -----------------------------*/

static struct ion_heap **heaps;
static void *carveout_ptr = NULL;
static void *chunk_ptr = NULL;

static struct ion_platform_heap default_heaps[] = {
	{
		.id	= ION_HEAP_TYPE_SYSTEM,
		.type = ION_HEAP_TYPE_SYSTEM,
		.name = "system-heap",
	},
	{
		.id	= ION_HEAP_TYPE_SYSTEM_CONTIG,
		.type = ION_HEAP_TYPE_SYSTEM_CONTIG,
		.name = "system-contig-heap",
	},
	{
		.id	= ION_HEAP_TYPE_CARVEOUT,
		.type = ION_HEAP_TYPE_CARVEOUT,
		.name = "carveout",
		.size = SZ_4M,
	},
	{
		.id	= ION_HEAP_TYPE_CHUNK,
		.type = ION_HEAP_TYPE_CHUNK,
		.name = "chunk",
		.size = SZ_4M,
		.align = SZ_16K,
		.priv  = (void *)(SZ_16K),
	}
};

static struct ion_platform_data ion_pdata = {
	.nr    = ARRAY_SIZE(default_heaps),
	.heaps = default_heaps,
};

static void _ion_device_clean(void)
{
	int i;

	for (i = 0; i < ion_pdata.nr; ++i) {
		ion_heap_destroy(heaps[i]);
	}

	kfree(heaps);

	if (carveout_ptr) {
		free_pages_exact(carveout_ptr, default_heaps[ION_HEAP_TYPE_CARVEOUT].size);
		carveout_ptr = NULL;
	}

	if (chunk_ptr) {
		free_pages_exact(chunk_ptr, default_heaps[ION_HEAP_TYPE_CHUNK].size);
		chunk_ptr = NULL;
	}
}

/*---------------------- ion-share and ion driver ----------------------------*/

static int hyper_ion_probe(struct platform_device *pdev)
{
	int i, err;
	// struct ion_platform_data *pdata = pdev->dev.platform_data;

	ion_dev = ion_device_create(NULL);

	if (IS_ERR(ion_dev)) {
		return PTR_ERR(ion_dev);
	}

	heaps = kcalloc(ion_pdata.nr, sizeof(struct ion_heap *), GFP_KERNEL);
	if (!heaps) {
		return -ENOMEM;
	}

	for (i = 0; i < ion_pdata.nr; i++) {
		struct ion_platform_heap *heap_data = &ion_pdata.heaps[i];

		if (heap_data->type == ION_HEAP_TYPE_CARVEOUT && !heap_data->base)
			continue;

		if (heap_data->type == ION_HEAP_TYPE_CHUNK && !heap_data->base)
			continue;

		heaps[i] = ion_heap_create(heap_data);
		if (IS_ERR_OR_NULL(heaps[i])) {
			err = PTR_ERR(heaps[i]);
			goto err;
		}
		ion_device_add_heap(ion_dev, heaps[i]);
	}
	
	(void)_ion_share_device_create(pdev);

	pr_notice("ion: started\n");
	return 0;

err:
	_ion_device_clean();

	return err;
}

static int hyper_ion_remove(struct platform_device *pdev)
{
	// struct ion_platform_data *pdata = pdev->dev.platform_data;

	_ion_device_clean();

	ion_device_destroy(ion_dev);

	_ion_share_device_remove(pdev);

	pr_notice("ion: removed\n");
	return 0;
}

static struct platform_driver  ion_share_driver = {
	.probe  = hyper_ion_probe,
	.remove = hyper_ion_remove,
	.driver = { .name = "ion-hyper" }
};

static struct platform_device *ion_share_device;

static int __init ion_module_init(void)
{
	ion_share_device = platform_device_register_simple("ion-hyper", -1, NULL, 0);

	if (IS_ERR(ion_share_device)) {
		return PTR_ERR(ion_share_device);
	}

	return platform_driver_probe(&ion_share_driver, hyper_ion_probe);
}

static void __exit ion_module_exit(void)
{
	platform_driver_unregister(&ion_share_driver);
	platform_device_unregister(ion_share_device);
}

module_init(ion_module_init);
module_exit(ion_module_exit);

MODULE_LICENSE("GPL");
