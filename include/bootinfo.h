/*
 * Non-machine dependent bootinfo structure.  Basic idea
 * borrowed from the m68k.
 *
 * Copyright (C) 1999 Cort Dougan <cort@ppc.kernel.org>
 */


#ifndef _PPC_BOOTINFO_H
#define _PPC_BOOTINFO_H

#define _MACH_prep	0x00000001
#define _MACH_Pmac	0x00000002	/* pmac or pmac clone (non-chrp) */
#define _MACH_chrp	0x00000004	/* chrp machine */
#define _MACH_mbx	0x00000008	/* Motorola MBX board */
#define _MACH_apus	0x00000010	/* amiga with phase5 powerup */
#define _MACH_fads	0x00000020	/* Motorola FADS board */
#define _MACH_rpxlite	0x00000040	/* RPCG RPX-Lite 8xx board */
#define _MACH_bseip	0x00000080	/* Bright Star Engineering ip-Engine */
#define _MACH_yk	0x00000100	/* Motorola Yellowknife */
#define _MACH_gemini	0x00000200	/* Synergy Microsystems gemini board */
#define _MACH_classic	0x00000400	/* RPCG RPX-Classic 8xx board */
#define _MACH_oak	0x00000800	/* IBM "Oak" 403 eval. board */
#define _MACH_walnut	0x00001000	/* IBM "Walnut" 405GP eval. board */

struct bi_record {
     unsigned long tag;			/* tag ID */
     unsigned long size;			/* size of record (in bytes) */
     unsigned long data[0];		/* data */
};

#define BI_FIRST		0x1010  /* first record - marker */
#define BI_LAST			0x1011	/* last record - marker */
#define BI_CMD_LINE		0x1012
#define BI_BOOTLOADER_ID	0x1013
#define BI_INITRD		0x1014
#define BI_SYSMAP		0x1015
#define BI_MACHTYPE		0x1016

#endif /* _PPC_BOOTINFO_H */

/* 
 * Local variables:
 * c-file-style: "K&R"
 * c-basic-offset: 5
 * End:
 */
