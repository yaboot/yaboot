#include "asm/ppc_asm.tmpl"
#include "asm/processor.h"

/*
 * Main entry point. should add code to clear BSS and more ...
 */
_GLOBAL(_start)
	lis	r10,edata@h
	ori	r10,r10,edata@l
	lis	r11,end@h
	ori	r11,r11,end@l
	subi	r10,r10,4
	subi	r11,r11,4
	li	r0, 0
1:	stwu	r0,4(r10)
	cmp	0,r10,r11
	bne	1b
	b	yaboot_start

/*
 * Returns (address we're running at) - (address we were linked at)
 * for use before the text and data are mapped to KERNELBASE.
 */
_GLOBAL(reloc_offset)
	mflr	r0
	bl	1f
1:	mflr	r3
	lis	r4,1b@ha
	addi	r4,r4,1b@l
	subf	r3,r4,r3
	mtlr	r0
	blr
