/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _MIPS_MACHVIRT_INT_H
#define _MIPS_MACHVIRT_INT_H


/*
 * Interrupts 0..15 are used for ISA compatible interrupts
 */
#define MACHVIRT_INT_BASE	0

/*
 * CPU core Interrupt Numbers
 */
#define MIPS_CPU_IRQ_BASE	16
#define MIPS_CPU_IRQ(x)		(MIPS_CPU_IRQ_BASE + (x))

#define SOFTINT0_IRQ		MIPS_CPU_IRQ(0)
#define SOFTINT1_IRQ		MIPS_CPU_IRQ(1)
#define HW0_IRQ			MIPS_CPU_IRQ(2)
#define HW1_IRQ			MIPS_CPU_IRQ(3)
#define HW2_IRQ			MIPS_CPU_IRQ(4)
#define HW3_IRQ			MIPS_CPU_IRQ(5)
#define HW4_IRQ			MIPS_CPU_IRQ(6)
#define TIMER_IRQ		MIPS_CPU_IRQ(7)		/* cpu timer */

#define MIPS_CPU_IRQS		(MIPS_CPU_IRQ(7) + 1 - MIPS_CPU_IRQ_BASE)

#define NR_IRQS			(MIPS_CPU_IRQ_BASE + MIPS_CPU_IRQS)

#endif /* !(_MIPS_MACHVIRT_INT_H) */

