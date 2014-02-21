/*
 * hypercall.S
 *
 *  Created on: 11.05.2011
 *      Author: janis
 */

#include <asm/hypercall.h>

void karma_hypercall0_svm(const unsigned long cmd){
    asm volatile(
            "vmmcall\n"
            : : "c"(cmd): "memory"
    );
}
void karma_hypercall1_svm(const unsigned long cmd, unsigned long * reg1){
    asm volatile(
            "vmmcall\n"
            : "=d"(*reg1): "c"(cmd), "0"(*reg1): "memory"
    );
}
void karma_hypercall2_svm(const unsigned long cmd, unsigned long * reg1, unsigned long * reg2){
    asm volatile(
            "vmmcall\n"
            : "=d"(*reg1), "=b"(*reg2): "c"(cmd), "0"(*reg1), "1"(*reg2): "memory"
    );
}
void karma_hypercall3_svm(const unsigned long cmd, unsigned long * reg1, unsigned long * reg2, unsigned long * reg3){
    asm volatile(
            "vmmcall\n"
            : "=d"(*reg1), "=b"(*reg2), "=S"(*reg3) : "c"(cmd), "0"(*reg1), "1"(*reg2), "2"(*reg3): "memory"
    );
}
void karma_hypercall4_svm(const unsigned long cmd, unsigned long * reg1, unsigned long * reg2, unsigned long * reg3, unsigned long * reg4){
    asm volatile(
            "vmmcall\n"
            : "=d"(*reg1), "=b"(*reg2), "=S"(*reg3), "=D"(*reg4): "c"(cmd), "0"(*reg1), "1"(*reg2), "2"(*reg3), "3"(*reg4): "memory"
    );
}


void karma_hypercall0_vmx(const unsigned long cmd){
    asm volatile(
            "vmcall\n"
            : : "c"(cmd): "memory"
    );
}
void karma_hypercall1_vmx(const unsigned long cmd, unsigned long * reg1){
    asm volatile(
            "vmcall\n"
            : "=d"(*reg1): "c"(cmd), "0"(*reg1): "memory"
    );
}
void karma_hypercall2_vmx(const unsigned long cmd, unsigned long * reg1, unsigned long * reg2){
    asm volatile(
            "vmcall\n"
            : "=d"(*reg1), "=b"(*reg2): "c"(cmd), "0"(*reg1), "1"(*reg2): "memory"
    );
}
void karma_hypercall3_vmx(const unsigned long cmd, unsigned long * reg1, unsigned long * reg2, unsigned long * reg3){
    asm volatile(
            "vmcall\n"
            : "=d"(*reg1), "=b"(*reg2), "=S"(*reg3) : "c"(cmd), "0"(*reg1), "1"(*reg2), "2"(*reg3): "memory"
    );
}
void karma_hypercall4_vmx(const unsigned long cmd, unsigned long * reg1, unsigned long * reg2, unsigned long * reg3, unsigned long * reg4){
    asm volatile(
            "vmcall\n"
            : "=d"(*reg1), "=b"(*reg2), "=S"(*reg3), "=D"(*reg4): "c"(cmd), "0"(*reg1), "1"(*reg2), "2"(*reg3), "3"(*reg4): "memory"
    );
}

struct karma_hypercall_s karma_hypercall_svm = {
    .hc_args0 = karma_hypercall0_svm,
    .hc_args1 = karma_hypercall1_svm,
    .hc_args2 = karma_hypercall2_svm,
    .hc_args3 = karma_hypercall3_svm,
    .hc_args4 = karma_hypercall4_svm,
};
struct karma_hypercall_s karma_hypercall_vmx = {
    .hc_args0 = karma_hypercall0_vmx,
    .hc_args1 = karma_hypercall1_vmx,
    .hc_args2 = karma_hypercall2_vmx,
    .hc_args3 = karma_hypercall3_vmx,
    .hc_args4 = karma_hypercall4_vmx,
};

struct karma_hypercall_s * karma_hypercall;

void karma_hypercall_init(void){
    unsigned int a, b, c, d, cmd = 0;

    asm volatile("cpuid"
            : "=a"(a),
            "=b"(b),
            "=c"(c),
            "=d"(d)
            : "0"(cmd));
    if((b == 0x6B636957) && (d == 0x69466465) && (c == 0x6f637361)){
        // WickedFiasco -> AMD
        karma_hypercall = &karma_hypercall_svm;
    } else {
        // FiascoWicked -> INTEL
        karma_hypercall = &karma_hypercall_vmx;
    }
}
