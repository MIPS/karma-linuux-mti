/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * hypercall.h
 */

#ifndef HYPERCALL_H_
#define HYPERCALL_H_

struct karma_hypercall_s{
    void (*hc_args0)(const unsigned long);
    void (*hc_args1)(const unsigned long, unsigned long *);
    void (*hc_args2)(const unsigned long, unsigned long *, unsigned long *);
    void (*hc_args3)(const unsigned long, unsigned long *, unsigned long *, unsigned long *);
    void (*hc_args4)(const unsigned long, unsigned long *, unsigned long *, unsigned long *, unsigned long *);
};

extern struct karma_hypercall_s karma_hypercall_vz;
extern struct karma_hypercall_s * karma_hypercall;

#endif /* HYPERCALL_H_ */
