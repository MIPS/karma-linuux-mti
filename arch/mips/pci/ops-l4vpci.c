/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>

#include <asm/l4.h>

static void l4pci_write(unsigned long addr, unsigned long val){
	karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(pci), addr | 1), &val);
}

static unsigned long l4pci_read(unsigned long addr){
	unsigned long ret = 0;
	karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(pci), addr), &ret);
	return ret;
}

int __init l4vpci_probe(void)
{
	if (l4pci_read(L4_PCI_PROBE) == -1)
		return -ENOENT;

	return 0;
}

void __init l4vpci_iomap(unsigned long io_map_base, unsigned long io_start, unsigned long io_end)
{
	unsigned long __temp_arg;

	struct l4_iomap a;
	a.addr = io_map_base + io_start;
	a.size = io_end - io_start + 1;
	barrier();
	__temp_arg = (unsigned long)__pa(&a);
	karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(pci),
				((L4_PCI_IOMAP) | 1)), &__temp_arg);
}

/*
 * l4vpci_pci_irq_enable
 * success: return 0
 * failure: return < 0
 */

int l4vpci_irq_enable(struct pci_dev *dev)
{
	unsigned char trigger, polarity;
	int irq;
	u8 pin = 0;
	unsigned flags;
	struct l4_pci_irq pirq;

	if (!dev)
		return -EINVAL;

	pin = dev->pin;
	if (!pin) {
		dev_warn(&dev->dev,
		         "No interrupt pin configured for device %s\n",
		         pci_name(dev));
		return 0;
	}
	pin--;


	if (!dev->bus) {
		dev_err(&dev->dev, "invalid (NULL) 'bus' field\n");
		return -ENODEV;
	}

	pirq.bus = dev->bus->number;
	pirq.devfn = PCI_SLOT(dev->devfn) << 16;
	pirq.pin = pin;
	pirq.trigger = 0;
	pirq.polarity = 0;
	pirq.ret = 0;
	barrier();
	l4pci_write(L4_PCI_ENABLE_IRQ, (u32)__pa(&pirq));
	trigger = pirq.trigger;
	polarity = pirq.polarity;
	irq = pirq.ret;
	if (irq < 0) {
		dev_warn(&dev->dev, "PCI INT %c: no GSI", 'A' + pin);
		// Interrupt Line values above 0xF are forbidden 
		return 0;
	}

	switch ((!!trigger) | ((!!polarity) << 1)) {
		case 0: flags = IRQF_TRIGGER_HIGH; break;
		case 1: flags = IRQF_TRIGGER_RISING; break;
		case 2: flags = IRQF_TRIGGER_LOW; break;
		case 3: flags = IRQF_TRIGGER_FALLING; break;
		default: flags = 0; break;
	}

	dev->irq = irq;

	dev_info(&dev->dev, "PCI INT %c -> GSI %u (%s, %s) -> IRQ %d\n",
	         'A' + pin, irq,
	         !trigger ? "level" : "edge",
	         polarity ? "low" : "high", dev->irq);

	return 0;
}

void l4vpci_irq_disable(struct pci_dev *dev)
{
	printk("%s: implement me\n", __func__);
}

/*
 * Functions for accessing PCI base (first 256 bytes) and extended
 * (4096 bytes per PCI function) configuration space with type 1
 * accesses.
 */
static int pci_conf1_read(unsigned int seg, unsigned int bus,
                         unsigned int devfn, int reg, int len, u32 *value)
{
	struct l4_pci_conf conf;
	conf.bus = bus;
	conf.df = (PCI_SLOT(devfn) << 16) | PCI_FUNC(devfn);
	conf.reg = reg;
	conf.value = 0;
	conf.len = len*8;
	conf.ret = 0;
	barrier();
	l4pci_write(L4_PCI_CONF1_READ, (u32)__pa(&conf));
	*value = conf.value;
	return conf.ret;
}

static int pci_conf1_write(unsigned int seg, unsigned int bus,
                           unsigned int devfn, int reg, int len, u32 value)
{
	struct l4_pci_conf conf;
	conf.bus = bus;
	conf.df = (PCI_SLOT(devfn) << 16) | PCI_FUNC(devfn);
	conf.reg = reg;
	conf.value = value;
	conf.len = len*8;
	conf.ret = 0;
	barrier();
	l4pci_write(L4_PCI_CONF1_WRITE, (u32)__pa(&conf));
	return conf.ret;
}

static int pci_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *value)
{
	return pci_conf1_read(pci_domain_nr(bus), bus->number,
	                      devfn, where, size, value);
}

static int pci_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 value)
{
	return pci_conf1_write(pci_domain_nr(bus), bus->number,
	                       devfn, where, size, value);
}

struct pci_ops l4vpci_ops = {
	.read = pci_read,
	.write = pci_write,
};
