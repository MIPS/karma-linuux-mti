#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <asm/irq.h>

#include <asm/l4.h>

//XXX: removed lock in favour of RT_PREEMPT patch, which complained about
//sleeping function in areas that must not sleep (interrupt handlers)
//static DEFINE_SPINLOCK(l4_irq_controller_lock);

struct karma_gic_chip_data {
	unsigned int irq_offset;
	void __iomem *base;
};

struct karma_gic_shared_state {
	u32 _enabled;
	u32 _level_trigered;
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
static void karma_gic_ack_irq(unsigned int irq)
{
	//u32 mask = 1 << (irq % 32);

	//printk("%s %x\n", __func__, irq);

	//spin_lock(&l4_irq_controller_lock);
	//l4_write(L4_GIC_ACK, mask);
	//spin_unlock(&l4_irq_controller_lock);
}

static void karma_gic_mask_irq(unsigned int irq)
{
	struct karma_gic_shared_state * shared_state = (struct karma_gic_shared_state *)karma_gic_data[0].base;
	u32 mask = 1 << (irq % 32);
	shared_state->_enabled &= ~mask;
	//printk("%s %x\n", __func__, irq);

	//spin_lock(&l4_irq_controller_lock);
	//l4_write(L4_GIC_DISABLE, irq);
	//spin_unlock(&l4_irq_controller_lock);
}

static void karma_gic_unmask_irq(unsigned int irq)
{
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

	//printk("%s %x\n", __func__, irq);

	//spin_lock(&l4_irq_controller_lock);
//	l4_write(L4_GIC_ENABLE, irq);
	//spin_unlock(&l4_irq_controller_lock);
}

#ifdef CONFIG_SMP
static int karma_gic_set_cpu(unsigned int irq, const struct cpumask *mask_val)
{
	unsigned int cpu = first_cpu(*mask_val);
	u32 val = 0;

	//spin_lock(&l4_irq_controller_lock);
	//irq_desc[irq].cpu = cpu;
	val |= 1 << (cpu);
	karma_gic_write(karma_gic_df_set_cpu, val);
	//spin_unlock(&l4_irq_controller_lock);
	return 0;
}
#endif

static struct irq_chip karma_gic_chip = {
	.name		= "GIC",
	//.irq_ack	= karma_gic_ack_irq,
	.irq_disable	= karma_gic_mask_irq, // TODO!
	.irq_mask_ack	= karma_gic_ack_irq,
	.irq_mask	= karma_gic_mask_irq,
	.irq_unmask	= karma_gic_unmask_irq,
//#ifdef CONFIG_SMP
//	.set_affinity	= karma_gic_set_cpu,
//#endif
};


void __init karma_gic_init(unsigned int gic_nr, unsigned int irq_start)
{
	unsigned int i;
	printk("[KARMA_GIC] %s\n gic_nr %u irq_start %u\n", __func__, gic_nr, irq_start);
	/*
	 * Initialize Karma Gic
	 */
	//karma_gic_write(karma_gic_df_init, gic_nr);

	karma_gic_data[gic_nr].base = (void*)karma_gic_read(karma_gic_df_get_base_reg);
	printk("[KARMA_GIC] %s\n base %p\n", __func__, karma_gic_data[gic_nr].base);
	karma_gic_data[gic_nr].base = ioremap((int)karma_gic_data[gic_nr].base, 0x1000);

	printk("[KARMA_GIC] %s\n base %p\n", __func__, karma_gic_data[gic_nr].base);

	karma_gic_data[gic_nr].irq_offset = irq_start;

	/*
	 * Setup the Linux IRQ subsystem.
	 */
	for (i = irq_start; i < karma_gic_data[gic_nr].irq_offset + L4_MAX_IRQ; i++) {
		irq_set_chip_and_handler_name(i, &karma_gic_chip, handle_level_irq, "karma_gic");
		enable_irq(i);
	}
}

