/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 *  Machine specific IO port address definition for generic.
 *  Written by Osamu Tomita <tomita@cinet.co.jp>
 */
#ifndef __LINUX_KARMA_TIMER_H
#define __LINUX_KARMA_TIMER_H

#include <linux/param.h>
#include <linux/spinlock.h>
#include <linux/timex.h>

/* i8253A PIT registers */
#define PIT_MODE	0x43
#define PIT_CH0		0x40
#define PIT_CH2		0x42

#define PIT_LATCH	((PIT_TICK_RATE + HZ/2) / HZ)

extern raw_spinlock_t karma_timer_lock;
extern struct clock_event_device karma_timer_clockevent;
extern void clockevent_karma_init(bool oneshot);

extern void setup_karma_timer(void);

#endif /* __LINUX_KARMA_TIMER_H */
