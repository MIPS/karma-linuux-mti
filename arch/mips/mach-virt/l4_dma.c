/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/printk.h>

#include <asm/io.h>
#include <asm/l4.h>

dma_addr_t l4_dma_base = -1;

void __init l4_dma_init(void)
{
#ifndef CONFIG_KARMA_L4_DMA
#else
	karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(mem),
			karma_mem_df_dma_base), (unsigned long *)&l4_dma_base);

	if (l4_dma_base == -1)
		pr_err("Failed to init L4 dma_base'n");
#endif
}
