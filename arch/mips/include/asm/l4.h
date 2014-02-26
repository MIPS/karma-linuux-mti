/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef L4_INCLUDED
#define L4_INCLUDED

#include <karma/karma_devices.h> // this includes karma/server/src/devices/devices_list.h

inline static void karma_set_reg_count(unsigned long * cmd, unsigned long reg_count){
	reg_count &= 0x7;
	reg_count <<= 24;
	*cmd |= reg_count;
}

#include "hypercall.h"

static inline void karma_hypercall0(const unsigned long cmd){
	karma_hypercall->hc_args0(cmd);
}
static inline void karma_hypercall1(unsigned long cmd, unsigned long * reg1){
	karma_set_reg_count(&cmd, 1);
	karma_hypercall->hc_args1(cmd, reg1);
}
static inline void karma_hypercall2(unsigned long cmd, unsigned long * reg1, unsigned long * reg2){
	karma_set_reg_count(&cmd, 2);
	karma_hypercall->hc_args2(cmd, reg1, reg2);
}
static inline void karma_hypercall3(unsigned long cmd, unsigned long * reg1, unsigned long * reg2, unsigned long * reg3){
	karma_set_reg_count(&cmd, 3);
	karma_hypercall->hc_args3(cmd, reg1, reg2, reg3);
}
static inline void karma_hypercall4(unsigned long cmd, unsigned long * reg1, unsigned long * reg2, unsigned long * reg3, unsigned long * reg4){
	karma_set_reg_count(&cmd, 4);
	karma_hypercall->hc_args4(cmd, reg1, reg2, reg3, reg4);
}

#define KARMA_WRITE_IMPL(dev, addr, val)\
	karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(dev), ((addr) | 1)), &val)

#define KARMA_READ_IMPL(dev, addr)\
	unsigned long ret = 0;\
	karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(dev), addr), &ret);\
	return ret




#endif
