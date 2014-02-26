/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */

#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <linux/bootmem.h>

#include <asm/mips-boards/generic.h>

extern void l4_dma_init(void);

const char *get_system_type(void)
{
	return "MIPS Virtual Platform";
}

void __init plat_mem_setup(void)
{
	/*
	 * Load the builtin devicetree. This causes the chosen node to be
	 * parsed resulting in our memory appearing
	 */
	__dt_setup_arch(&__dtb_start);

	l4_dma_init();
}

void __init device_tree_init(void)
{
	unsigned long base, size;
	void *fdt_copy;

	if (!initial_boot_params)
		return;

	base = virt_to_phys((void *)initial_boot_params);
	size = be32_to_cpu(initial_boot_params->totalsize);

	/* Before we do anything, lets reserve the dt blob */
	reserve_bootmem(base, size, BOOTMEM_DEFAULT);

	/* The strings in the flattened tree are referenced directly by the
	 * device tree, so copy the flattened device tree from init memory
	 * to regular memory.
	 */
	fdt_copy = early_init_dt_alloc_memory_arch(size, 8);
	if (fdt_copy == NULL)
		panic("Could not allocate initial_boot_params\n");
	memcpy(fdt_copy, initial_boot_params, size);
	initial_boot_params = fdt_copy;

	unflatten_device_tree();
}

static int __init plat_of_setup(void)
{
	if (!of_have_populated_dt())
		panic("device tree not present");

	if (of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL))
		panic("failed to populate DT\n");

	return 0;
}

arch_initcall(plat_of_setup);
