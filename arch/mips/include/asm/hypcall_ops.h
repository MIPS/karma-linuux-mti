/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */

#ifndef _HYPCALL_OPS_H
#define _HYPCALL_OPS_H

/*
 * Definition of HYPCALL codes for interface between VZ VMM and VZ Guest
 */

/* max value 0x3ff */
#define HYPCALL_KARMA_DEV_OP    (0x000)

#ifdef __ASSEMBLER__

#define ASM_HYPCALL(code)       .word( 0x42000028 | (code<<11) )

#else /* __ASSEMBLER__ */

#ifndef __STR
#define __STR(x) #x
#endif
#ifndef STR
#define STR(x) __STR(x)
#endif

#define ASM_HYPCALL_STR(code)   STR(.word( 0x42000028 | (code<<11) ))

#endif /* __ASSEMBLER__ */

#endif /* _HYPCALL_OPS_H */
