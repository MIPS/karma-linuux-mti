/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/of_irq.h>

#include <asm/irq_cpu.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>

extern int __init karma_gic_of_init(struct device_node *node,
			       struct device_node *parent);

asmlinkage void plat_irq_dispatch(void)
{
	u32 pending = read_c0_status() & read_c0_cause() & ST0_IM;

	if (pending & STATUSF_IP7)
		do_IRQ(TIMER_IRQ);

	else if (pending & STATUSF_IP2) // cascaded karma_gic device
		do_IRQ(HW0_IRQ);

	else
		spurious_interrupt();
}

static struct of_device_id __initdata of_irq_ids[] = {
	{ .compatible = "mti,cpu-interrupt-controller", .data = mips_cpu_intc_init },
	{ .compatible = "karma,karma-gic-intc", .data = karma_gic_of_init },
	{},
};

void __init arch_init_irq(void)
{
	of_irq_init(of_irq_ids);
}
