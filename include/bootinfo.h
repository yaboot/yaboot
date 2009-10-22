/*
 *  bootinfo.h - Non-machine dependent bootinfo structure.  Basic idea from m68k
 *
 *  Copyright (C) 1999 Cort Dougan <cort@ppc.kernel.org>
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
#define _MACH_bplan	0x00002000	/* Pegasos/Efika, broken partition #s */

#endif /* _PPC_BOOTINFO_H */

extern int _machine;

/*
 * Local variables:
 * c-file-style: "k&r"
 * c-basic-offset: 5
 * End:
 */
