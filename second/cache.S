/*
 *  cache.S - PowerPC version 
 *
 *  Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Copyright (C) 1996 Paul Mackerras
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "asm/ppc_asm.tmpl"
#include "asm/processor.h"
	
/*
 * Write any modified data cache blocks out to memory
 * and invalidate the corresponding instruction cache blocks.
 * This is a no-op on the 601.
 *
 * flush_icache_range(unsigned long start, unsigned long stop)
 */

CACHE_LINE_SIZE = 32
LG_CACHE_LINE_SIZE = 5

	.text
	.globl	flush_icache_range
	.type	flush_icache_range,@function
flush_icache_range:
	mfspr	r5,PVR
	rlwinm	r5,r5,16,16,31
	cmpi	0,r5,1
	beqlr				/* for 601, do nothing */
	li	r5,CACHE_LINE_SIZE-1
	andc	r3,r3,r5
	subf	r4,r3,r4
	add	r4,r4,r5
	srwi.	r4,r4,LG_CACHE_LINE_SIZE
	beqlr
	mtctr	r4
	mr	r6,r3
1:	dcbst	0,r3
	addi	r3,r3,CACHE_LINE_SIZE
	bdnz	1b
	sync				/* wait for dcbst's to get to ram */
	mtctr	r4
2:	icbi	0,r6
	addi	r6,r6,CACHE_LINE_SIZE
	bdnz	2b
	sync
	isync
	blr

	.text
	.globl	turn_off_mmu
	.type	turn_off_mmu,@function
turn_off_mmu:
	lis	r0,1f@h
	ori	r0,r0,1f@l
	mtspr	SRR0,r0
	mfmsr	r0
	lis	r2,(~(MSR_DR|MSR_IR))@h
	ori	r2,r2,(~(MSR_DR|MSR_IR))@l
	and	r0,r0,r2
	mtspr	SRR1,r0
	rfi
1:
	blr
