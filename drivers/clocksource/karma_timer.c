/*
 * karma_timer.c
 *
 * based on i8253 PIT clocksource (drivers/clocksource/i8253.c)
 */
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/timex.h>
#include <linux/module.h>
#include <linux/i8253.h>
#include <linux/smp.h>

#include <asm/l4.h> // import karma/server/src/devices/devices_list.h

static void karma_timer_write(unsigned long addr, unsigned long val){
	KARMA_WRITE_IMPL(timer, addr, val);
}

static unsigned long karma_timer_read(unsigned long addr){
	KARMA_READ_IMPL(timer, addr);
}

//XXX: don't know if those are actually needed
//     leaving them in as the karma timer is only needed during bootup
//     afterwards the apic timer takes care of it
DEFINE_RAW_SPINLOCK(karma_timer_lock);
EXPORT_SYMBOL(karma_timer_lock);

/*
 */
static cycle_t karma_read(struct clocksource *cs)
{
	/* NOP */
}

static struct clocksource karma_timer_cs = {
	.name		= "karma_timer",
	.rating		= 110,
	.read		= karma_read,
	.mask		= CLOCKSOURCE_MASK(32),
};

int __init clocksource_karma_init(void)
{
	return clocksource_register_hz(&karma_timer_cs, PIT_TICK_RATE);
}
/*
 * Initialize the Karma timer.
 */
static void init_karma_timer(enum clock_event_mode mode,
			   struct clock_event_device *evt)
{
	unsigned long crtl, _ms;
	crtl = _ms = 0;

	raw_spin_lock(&karma_timer_lock);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		crtl = L4_TIMER_ENABLE_PERIODIC;
		_ms = LATCH;
		break;

	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
		if (evt->mode == CLOCK_EVT_MODE_PERIODIC ||
		    evt->mode == CLOCK_EVT_MODE_ONESHOT) {
			// time has not been enabled, so we dont disable it now
			crtl = _ms = 0;
		}
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		/* One shot setup */
		crtl = L4_TIMER_ENABLE_ONESHOT;
		break;

	case CLOCK_EVT_MODE_RESUME:
		/* Nothing to do here */
		break;
	}
	if(crtl)
		karma_timer_write(L4_TIMER_ENABLE, crtl);
	if(_ms)
		karma_timer_write(L4_TIMER_INIT, 1);
	raw_spin_unlock(&karma_timer_lock);
}

/*
 * Program the next event in oneshot mode
 *
 * Delta is given in Karma timer ticks
 */
static int karma_next_event(unsigned long delta, struct clock_event_device *evt)
{
	raw_spin_lock(&karma_timer_lock);
	karma_timer_write(L4_TIMER_INIT, delta);
	karma_timer_write(L4_TIMER_ENABLE, L4_TIMER_ENABLE_ONESHOT);
	raw_spin_unlock(&karma_timer_lock);

	return 0;
}

/*
 * On UP the Karma timer can serve all of the possible timer functions. On SMP systems
 * it can be solely used for the global tick.
 */
struct clock_event_device karma_timer_clockevent = {
	.name		= "karma_timer",
	.features	= CLOCK_EVT_FEAT_PERIODIC,
	.set_mode	= init_karma_timer,
	.set_next_event = karma_next_event,
};

/*
 * Initialize the conversion factor and the min/max deltas of the clock event
 * structure and register the clock event source with the framework.
 */
void __init clockevent_karma_init(bool oneshot)
{
	if (oneshot)
		karma_timer_clockevent.features |= CLOCK_EVT_FEAT_ONESHOT;
	/*
	 * Start pit with the boot cpu mask. x86 might make it global
	 * when it is used as broadcast device later.
	 */
	karma_timer_clockevent.cpumask = cpumask_of(smp_processor_id());

	clockevents_config_and_register(&karma_timer_clockevent, PIT_TICK_RATE,
					0xF, 0x7FFF);
}

