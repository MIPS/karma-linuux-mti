/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */

#ifndef __ASM_MACH_VIRT_DMA_COHERENCE_H
#define __ASM_MACH_VIRT_DMA_COHERENCE_H

#include <asm/l4.h>

extern dma_addr_t l4_dma_base;

struct device;

static inline dma_addr_t plat_map_dma_mem(struct device *dev, void *addr,
	size_t size)
{
	unsigned long guest_phys = virt_to_phys(addr);
	unsigned long dma_phys = 0;

	karma_hypercall2(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(mem),
			karma_mem_df_guest_phys_to_host_phys), &guest_phys, &dma_phys);
	return (dma_addr_t)dma_phys;
}

static inline dma_addr_t plat_map_dma_mem_page(struct device *dev,
	struct page *page)
{
	return l4_dma_base + page_to_phys(page);
}

static inline unsigned long plat_dma_addr_to_phys(struct device *dev,
	dma_addr_t dma_addr)
{
	return dma_addr - l4_dma_base;
}

static inline void plat_unmap_dma_mem(struct device *dev, dma_addr_t dma_addr,
	size_t size, enum dma_data_direction direction)
{
}

static inline int plat_dma_supported(struct device *dev, u64 mask)
{
#ifndef CONFIG_KARMA_L4_DMA
	return 0;
#else
	/*
	 * we fall back to GFP_DMA when the mask isn't all 1s,
	 * so we can't guarantee allocations that must be
	 * within a tighter range than GFP_DMA..
	 */
	if (mask < DMA_BIT_MASK(24))
		return 0;

	return 1;
#endif
}

static inline void plat_extra_sync_for_device(struct device *dev)
{
}

static inline int plat_dma_mapping_error(struct device *dev,
					 dma_addr_t dma_addr)
{
	return 0;
}

static inline int plat_device_is_coherent(struct device *dev)
{
#ifdef CONFIG_DMA_COHERENT
	return 1;
#else
	return coherentio;
#endif
}

#endif /* __ASM_MACH_VIRT_DMA_COHERENCE_H */

