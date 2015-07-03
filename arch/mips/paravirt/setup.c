/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2013 Cavium, Inc.
 */

#include <linux/kernel.h>
#ifdef CONFIG_KARMA_L4
#include <asm/l4.h>
#else
#include <linux/kvm_para.h>
#endif

#include <asm/reboot.h>
#include <asm/bootinfo.h>
#include <asm/smp-ops.h>
#include <asm/time.h>

extern struct plat_smp_ops paravirt_smp_ops;

const char *get_system_type(void)
{
	return "MIPS Para-Virtualized Guest";
}

static inline u32 get_counter_resolution(void)
{
        u32 res;

        __asm__ __volatile__(
                ".set   push\n"
                ".set   mips32r2\n"
                "rdhwr  %0, $3\n"
                ".set pop\n"
                : "=&r" (res)
                : /* no input */
                : "memory");

        return res;
}

void __init plat_time_init(void)
{
#ifdef CONFIG_KARMA_L4
	unsigned long temp_arg = 0;
	karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(karma), karma_df_get_khz_cpu), &temp_arg);
	mips_hpt_frequency = temp_arg * 1000 / get_counter_resolution();
#else
	mips_hpt_frequency = kvm_hypercall0(KVM_HC_MIPS_GET_CLOCK_FREQ);
#endif

	preset_lpj = mips_hpt_frequency / (2 * HZ);
}

static void pv_machine_halt(void)
{
#ifdef CONFIG_KARMA_L4
	karma_hypercall_exit_op();
#else
	kvm_hypercall0(KVM_HC_MIPS_EXIT_VM);
#endif
}

/*
 * Early entry point for arch setup
 */
void __init prom_init(void)
{
	int i;
	int argc = fw_arg0;
	char **argv = (char **)fw_arg1;

#ifdef CONFIG_32BIT
	set_io_port_base(KSEG1ADDR(0x1e000000));
#else /* CONFIG_64BIT */
	set_io_port_base(PHYS_TO_XKSEG_UNCACHED(0x1e000000));
#endif

	for (i = 0; i < argc; i++) {
		strlcat(arcs_cmdline, argv[i], COMMAND_LINE_SIZE);
		if (i < argc - 1)
			strlcat(arcs_cmdline, " ", COMMAND_LINE_SIZE);
	}
	_machine_halt = pv_machine_halt;
	register_smp_ops(&paravirt_smp_ops);
}

void __init plat_mem_setup(void)
{
	/* Do nothing, the "mem=???" parser handles our memory. */
}

void __init prom_free_prom_memory(void)
{
}
