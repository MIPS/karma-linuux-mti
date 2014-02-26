/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Imagination Technologies Ltd.
 *
 * This file incorporates work covered by the following copyright notice:
 */

/*
 * karma-timer initialization.
 * (c) Steffen Liebergeld (steffen@sec.t-labs.tu-berlin.de)
 * 
 * based on 8253/PIT functions
 *
 */
#include <linux/clockchips.h>
#include <linux/module.h>
#include <linux/timex.h>
#include <linux/karma_timer.h>

//#include <asm/hpet.h>
#include <asm/time.h>
//#include <asm/smp.h>

/*
 * HPET replaces the PIT, when enabled. So we need to know, which of
 * the two timers is used
 */
struct clock_event_device *global_clock_event;

void __init setup_karma_timer(void)
{
	clockevent_karma_init(true);
	global_clock_event = &karma_timer_clockevent;
}

static int __init init_karma_clocksource(void)
{
	 /*
	  * Several reasons not to register PIT as a clocksource:
	  *
	  * - On SMP PIT does not scale due to i8253_lock
	  * - when HPET is enabled
	  * - when local APIC timer is active (PIT is switched off)
	  */
	if (num_possible_cpus() > 1 || // is_hpet_enabled() ||
	    karma_timer_clockevent.mode != CLOCK_EVT_MODE_PERIODIC)
		return 0;

	return clocksource_karma_init();
}
arch_initcall(init_karma_clocksource);
