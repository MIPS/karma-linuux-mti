#include <linux/pci.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/compiler.h>
#include <asm/pci.h>
#include <asm/pci_x86.h>

#include <asm/l4.h>

struct l4_pci_conf
{
	unsigned int bus, df, value;
	int reg, len;
	unsigned int ret;
};

struct l4_pci_irq
{
	unsigned int bus, devfn;
	int pin;
	unsigned char trigger, polarity;
	int ret;
};

//DEFINE_SPINLOCK(pci_config_lock);

unsigned int pci_probe;
unsigned int pci_early_dump_regs;
int noioapicquirk;
int noioapicreroute = 0;


void l4pci_write(unsigned long addr, unsigned long val){
	karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(pci), addr | 1), &val);
}
unsigned long l4pci_read(unsigned long addr){
	unsigned long ret = 0;
	karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(pci), addr), &ret);
	return ret;
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

static struct pci_ops l4vpci_ops = {
	.read = pci_read,
	.write = pci_write,
};

static int __init l4vpci_init(void)
{
	struct pci_dev *dev = NULL;
	struct pci_sysdata *sd;
	int err;

	if(l4pci_read(L4_PCI_PROBE) == -1)
		return -ENOENT;

	err = -ENOMEM;
	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd)
		goto free_cap;

	pci_scan_bus(0, &l4vpci_ops, sd);

	printk(KERN_INFO "PCI: Using L4-IO for IRQ routing\n");

	for_each_pci_dev(dev)
		l4vpci_irq_enable(dev);

	pcibios_resource_survey();

	return 0;

free_cap:

	return err;
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	int err;

	if ((err = pci_enable_resources(dev, mask)) < 0)
		return err;

	return l4vpci_irq_enable(dev);
}

unsigned int pcibios_assign_all_busses(void)
{
	return 1;
}

int early_pci_allowed(void)
{
	return 0;
}

void early_dump_pci_devices(void)
{
	printk("%s: unimplemented\n", __func__);
}

u32 read_pci_config(u8 bus, u8 slot, u8 func, u8 offset)
{
	printk("%s: unimplemented\n", __func__);
	return 0;
}

u8 read_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset)
{
	printk("%s: unimplemented\n", __func__);
	return 0;
}

u16 read_pci_config_16(u8 bus, u8 slot, u8 func, u8 offset)
{
	printk("%s: unimplemented\n", __func__);
	return 0;
}

void write_pci_config(u8 bus, u8 slot, u8 func, u8 offset, u32 val)
{
	printk("%s: unimplemented\n", __func__);
}

char * __init pcibios_setup(char *str)
{
	return str;
}

void pcibios_disable_device (struct pci_dev *dev)
{
	//l4vpci_irq_disable(dev);
}

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */
void pcibios_fixup_bus(struct pci_bus *b)
{
	pci_read_bridge_bases(b);
}


subsys_initcall(l4vpci_init);
int __init pci_legacy_init(void)
{
	return 0;
}

void __init pcibios_irq_init(void)
{
}

void __init pcibios_fixup_irqs(void)
{
}


