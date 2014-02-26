/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */

#include <linux/types.h>

#include <asm/mipsregs.h>
#include <asm/time.h>

void __init plat_time_init(void)
{
	unsigned int freq;

	freq = CONFIG_VIRT_HOST_FREQ * 1000000;

	mips_hpt_frequency = freq / 2;

	printk("CPU clock frequency %d.%02d MHz\n", freq/1000000,
	       (freq%1000000)*100/1000000);
}
