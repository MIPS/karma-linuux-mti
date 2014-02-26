/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>

#include <asm/irq_cpu.h>
#include <asm/irq.h>

#include <asm/l4.h>

//XXX: removed lock in favour of RT_PREEMPT patch, which complained about
//sleeping function in areas that must not sleep (interrupt handlers)
//static DEFINE_SPINLOCK(l4_irq_controller_lock);

struct karma_gic_chip_data {
	unsigned int irq_offset;
	void __iomem *base;
};

static void karma_gic_write(unsigned long addr, unsigned long val){
	KARMA_WRITE_IMPL(gic, addr, val);
}

static unsigned long karma_gic_read(unsigned long addr){
	KARMA_READ_IMPL(gic, addr);
}

static struct karma_gic_chip_data karma_gic_data[L4_MAX_GIC_NR];

static inline unsigned int karma_gic_irq(unsigned int irq)
{
	struct karma_gic_chip_data *gic_data = irq_get_chip_data(irq);
	return irq - gic_data->irq_offset;
}

/*
 * Routines to acknowledge, disable and enable interrupts
 *
 * Linux assumes that when we're done with an interrupt we need to
 * unmask it, in the same way we need to unmask an interrupt when
 * we first enable it.
 *
 * The GIC has a separate notion of "end of interrupt" to re-enable
 * an interrupt after handling, in order to support hardware
 * prioritisation.
 *
 * We can make the GIC behave in the way that Linux expects by making
 * our "acknowledge" routine disable the interrupt, then mark it as
 * complete.
 */
static void karma_gic_mask_ack_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;
	struct karma_gic_shared_state * shared_state = (struct karma_gic_shared_state *)karma_gic_data[0].base;
	u32 mask = 1 << (irq % 32);
	shared_state->_enabled &= ~mask;
#if 1 // KYMA GIC_PENDING
	karma_gic_write(karma_gic_df_ack, d->irq);
#endif
}

static void karma_gic_mask_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;
	struct karma_gic_shared_state * shared_state = (struct karma_gic_shared_state *)karma_gic_data[0].base;
	u32 mask = 1 << (irq % 32);
	shared_state->_enabled &= ~mask;
}

static void karma_gic_unmask_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;
	struct karma_gic_shared_state * shared_state = (struct karma_gic_shared_state *)karma_gic_data[0].base;
	u32 mask = 1 << (irq % 32);

	// if the interrupt is level triggered karma
	// has to unmask the interrupt explicitely
	// in all other cases no hypercall is needed
	if(mask & shared_state->_level_trigered){
		karma_gic_write(karma_gic_df_enable, irq);
	} else {
		shared_state->_enabled |= mask;
	}
}

static struct irq_chip karma_gic_chip = {
	.name		= "GIC",
	.irq_disable	= karma_gic_mask_irq, // TODO!
	.irq_mask_ack	= karma_gic_mask_ack_irq,
	.irq_mask	= karma_gic_mask_irq,
	.irq_unmask	= karma_gic_unmask_irq,
};


void __init karma_gic_init(unsigned int gic_nr, unsigned int irq_start)
{
	printk("[KARMA_GIC] %s\n gic_nr %u irq_start %u\n", __func__, gic_nr, irq_start);

	karma_gic_data[gic_nr].base = (void*)karma_gic_read(karma_gic_df_get_base_reg);
	printk("[KARMA_GIC] %s\n base %p\n", __func__, karma_gic_data[gic_nr].base);

	karma_gic_data[gic_nr].base = ioremap((int)karma_gic_data[gic_nr].base, 0x1000);
	printk("[KARMA_GIC] %s\n base %p\n", __func__, karma_gic_data[gic_nr].base);

	karma_gic_data[gic_nr].irq_offset = irq_start;
}

static void karma_gic_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	u32 pending = 0;

#if 1 // KYMA GIC_PENDING
	struct karma_gic_shared_state * shared_state = (struct karma_gic_shared_state *)karma_gic_data[0].base;
	pending = shared_state->_pending;
#endif

	if (pending) {
		struct irq_domain *domain = irq_get_handler_data(irq);
		generic_handle_irq(irq_find_mapping(domain, __ffs(pending)));
	} else {
		spurious_interrupt();
	}
}

static int karma_gic_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw)
{
	irq_set_chip_and_handler_name(irq, &karma_gic_chip, handle_level_irq, "karma_gic");

	return 0;
}

static const struct irq_domain_ops irq_domain_ops = {
	.xlate = irq_domain_xlate_onecell,
	.map = karma_gic_map,
};

int __init karma_gic_of_init(struct device_node *node,
			       struct device_node *parent)
{
	struct irq_domain *domain;
	int irq;

	karma_gic_init(0, 0);

	irq = irq_of_parse_and_map(node, 0);
	if (!irq)
		panic("Failed to get karma_gic IRQ");

	domain = irq_domain_add_legacy(node, L4_MAX_IRQ,
			MACHVIRT_INT_BASE, 0, &irq_domain_ops, NULL);
	if (!domain)
		panic("Failed to add irqdomain");

	irq_set_chained_handler(irq, karma_gic_irq_handler);
	irq_set_handler_data(irq, domain);

	return 0;
}
