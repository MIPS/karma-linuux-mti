/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/of_pci.h>
#include <linux/of_irq.h>

extern int __init l4vpci_probe(void);
extern void __init l4vpci_iomap(unsigned long io_map_base, unsigned long io_start, unsigned long io_end);

extern struct pci_ops l4vpci_ops;

static struct resource l4vpci_mem_resource = {
	.name = "Virtual l4vpci MEM",
	.flags = IORESOURCE_MEM,
};

static struct resource l4vpci_io_resource = {
	.name = "Virtual l4vpci IO",
	.flags = IORESOURCE_IO,
};

// KYMA TODO remove dependency on IOPORT_RESOURCE_END having to match
// KYMA "System Controller Ports" window size
// KYMA TODO setting IOPORT_RESOURCE_START to 0 causes issues with ide ...
#define PCI_IOSPACE_BASE	0x18000000
#define IOPORT_RESOURCE_START	0x00080000
#define IOPORT_RESOURCE_END	0x00083fff
#define IOMEM_RESOURCE_START	0x10000000
#define IOMEM_RESOURCE_END	0x17ffffff

static struct pci_controller l4vpci_pci_controller = {
	.pci_ops	= &l4vpci_ops,
	.mem_resource	= &l4vpci_mem_resource,
	.mem_offset	= 0x00000000UL,
	.io_resource	= &l4vpci_io_resource,
	.io_offset	= 0x00000000UL,
};

static int __init pcibios_init(void)
{
	int ret = l4vpci_probe();

	if (ret)
		return ret;

	l4vpci_iomap(PCI_IOSPACE_BASE, IOPORT_RESOURCE_START, IOPORT_RESOURCE_END);

	ioport_resource.start = IOPORT_RESOURCE_START;
	ioport_resource.end = IOPORT_RESOURCE_END;
	iomem_resource.start = IOMEM_RESOURCE_START;
	iomem_resource.end = IOMEM_RESOURCE_END;

	l4vpci_pci_controller.io_resource->start = IOPORT_RESOURCE_START;
	l4vpci_pci_controller.io_resource->end = IOPORT_RESOURCE_END;
	l4vpci_pci_controller.mem_resource->start = IOMEM_RESOURCE_START;
	l4vpci_pci_controller.mem_resource->end = IOMEM_RESOURCE_END;
	l4vpci_pci_controller.io_map_base = CKSEG1ADDR(PCI_IOSPACE_BASE);

	set_io_port_base((unsigned long)l4vpci_pci_controller.io_map_base);

	register_pci_controller(&l4vpci_pci_controller);

	return 0;
}

arch_initcall(pcibios_init);
