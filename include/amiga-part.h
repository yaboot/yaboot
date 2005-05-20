/*
 *  amiga-part.h - Structure of amiga partition table
 *
 *  Copyright (C) 2004 Sven Luther <luther@debian.org>
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

#define AMIGA_RDB_MAX	16
#define AMIGA_ID_RDB	0x5244534B	/* 'RDSK' */
#define AMIGA_ID_PART	0x50415254	/* 'PART' */
#define AMIGA_END	0xffffffff

#define AMIGA_ID		0
#define AMIGA_LENGTH		1
#define AMIGA_PARTITIONS	7
#define AMIGA_CYL		16
#define AMIGA_SECT		17
#define AMIGA_HEADS		18
#define AMIGA_RDBLIMIT		33


#define AMIGA_PART_NEXT		4
#define AMIGA_PART_FLAGS	8
#define AMIGA_PART_LOWCYL	41
#define AMIGA_PART_HIGHCYL	42
#define AMIGA_PART_DOSTYPE	48

#define	AMIGA_PART_BOOTABLE	0x1
#define	AMIGA_PART_NOMOUNT	0x2
