/* Fallback functions when the main IOMMU code is not compiled in. This
   code is roughly equivalent to i386. */
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/pci.h>
#include <linux/mm.h>

#include <asm/processor.h>
#include <asm/iommu.h>
#include <asm/dma.h>

#ifdef CONFIG_X86_L4
#include <asm/l4.h>
#endif

static int
check_addr(char *name, struct device *hwdev, dma_addr_t bus, size_t size)
{
	if (hwdev && !dma_capable(hwdev, bus, size)) {
		if (*hwdev->dma_mask >= DMA_BIT_MASK(32))
			printk(KERN_ERR
			    "nommu_%s: overflow %Lx+%zu of device mask %Lx\n",
				name, (long long)bus, size,
				(long long)*hwdev->dma_mask);
		return 0;
	}
	return 1;
}

static dma_addr_t nommu_map_page(struct device *dev, struct page *page,
				 unsigned long offset, size_t size,
				 enum dma_data_direction dir,
				 struct dma_attrs *attrs)
{
	dma_addr_t bus = page_to_phys(page) + offset;
	WARN_ON(size == 0);
	if (!check_addr("map_single", dev, bus, size))
		return DMA_ERROR_CODE;
	flush_write_buffers();
	return bus;
}

/* Map a set of buffers described by scatterlist in streaming
 * mode for DMA.  This is the scatter-gather version of the
 * above pci_map_single interface.  Here the scatter gather list
 * elements are each tagged with the appropriate dma address
 * and length.  They are obtained via sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *       The routine returns the number of addr/length pairs actually
 *       used, at most nents.
 *
 * Device ownership issues as mentioned above for pci_map_single are
 * the same here.
 */
#ifdef CONFIG_X86_L4
u32 dma_base = 0;
u32 dma_base_flag = 0;
#endif
static int nommu_map_sg(struct device *hwdev, struct scatterlist *sg,
			int nents, enum dma_data_direction dir,
			struct dma_attrs *attrs)
{
	struct scatterlist *s;
	int i;
#ifdef CONFIG_X86_L4
	u32 test = 0;
	unsigned long __temp_arg;
#endif

	WARN_ON(nents == 0 || sg[0].length == 0);

	for_each_sg(sg, s, nents, i) {
		BUG_ON(!sg_page(s));
		s->dma_address = sg_phys(s);
#ifdef CONFIG_X86_L4
		barrier();
#endif
		if (!check_addr("map_sg", hwdev, s->dma_address, s->length))
			return 0;
#ifdef CONFIG_X86_L4
		if(unlikely(!dma_base_flag))
		{
			__temp_arg = (unsigned long)virt_to_phys(&dma_base);
			karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(mem),
						karma_mem_df_dma_base), &__temp_arg);
			dma_base_flag = 1;
		}
		test = s->dma_address;
		s->dma_address = dma_base + s->dma_address;
		barrier();
#endif
		s->dma_length = s->length;
	}
	flush_write_buffers();
	return nents;
}

static void nommu_sync_single_for_device(struct device *dev,
			dma_addr_t addr, size_t size,
			enum dma_data_direction dir)
{
	flush_write_buffers();
}


static void nommu_sync_sg_for_device(struct device *dev,
			struct scatterlist *sg, int nelems,
			enum dma_data_direction dir)
{
	flush_write_buffers();
}

struct dma_map_ops nommu_dma_ops = {
	.alloc			= dma_generic_alloc_coherent,
	.free			= dma_generic_free_coherent,
	.map_sg			= nommu_map_sg,
	.map_page		= nommu_map_page,
	.sync_single_for_device = nommu_sync_single_for_device,
	.sync_sg_for_device	= nommu_sync_sg_for_device,
	.is_phys		= 1,
};
