/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */

#include <linux/init.h>

#include <asm/fw/fw.h>
#include <asm/l4.h>

void prom_putchar(char c)
{
	unsigned long val = (unsigned long)c;
	karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(ser),
				karma_ser_df_early_putchar), &val);
}

void __init prom_init(void)
{
	fw_init_cmdline();
	fw_meminit();
}
