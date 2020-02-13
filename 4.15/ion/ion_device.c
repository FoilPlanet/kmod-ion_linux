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

static struct ion_device *idev;
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

static int my_ion_probe(struct platform_device *pdev)
{
	int i, err;
	// struct ion_platform_data *pdata = pdev->dev.platform_data;

	idev = ion_device_create(NULL);

	if (IS_ERR(idev)) {
		return PTR_ERR(idev);
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
		ion_device_add_heap(idev, heaps[i]);
	}
	
	pr_notice("ion: started\n");
	return 0;

err:
	_ion_device_clean();

	return err;
}

static int my_ion_remove(struct platform_device *pdev)
{
	// struct ion_platform_data *pdata = pdev->dev.platform_data;

	_ion_device_clean();

	ion_device_destroy(idev);

	pr_notice("ion: removed\n");
	return 0;
}

static struct platform_device *ion_device;
static struct platform_driver ion_driver = {
	.probe  = my_ion_probe,
	.remove = my_ion_remove,
	.driver = { .name = "ion-mgnt" }
};

static int __init ion_module_init(void)
{
	ion_device = platform_device_register_simple("ion-mgnt", -1, NULL, 0);

	if (IS_ERR(ion_device)) {
		return PTR_ERR(ion_device);
	}

	return platform_driver_probe(&ion_driver, my_ion_probe);
}

static void __exit ion_module_exit(void)
{
	platform_driver_unregister(&ion_driver);
	platform_device_unregister(ion_device);
}

module_init(ion_module_init);
module_exit(ion_module_exit);

MODULE_LICENSE("GPL");
