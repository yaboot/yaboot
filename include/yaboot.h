/*
 *  Yaboot - secondary boot loader for Linux on PowerPC. 
 *
 *  Copyright (C) 2001 Ethan Benson
 *
 *  Copyright (C) 1999, 2000, 2001 Benjamin Herrenschmidt
 *  
 *  IBM CHRP support
 *  
 *  Copyright (C) 2001 Peter Bergner
 *
 *  portions based on poof
 *  
 *  Copyright (C) 1999 Marius Vollmer
 *  
 *  portions based on quik
 *  
 *  Copyright (C) 1996 Paul Mackerras.
 *  
 *  Because this program is derived from the corresponding file in the
 *  silo-0.64 distribution, it is also
 *  
 *  Copyright (C) 1996 Pete A. Zaitcev
 *                1996 Maurizio Plaza
 *                1996 David S. Miller
 *                1996 Miguel de Icaza
 *                1996 Jakub Jelinek
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

#ifndef __YABOOT_H__
#define __YABOOT_H__

#include "file.h"

struct boot_param_t {
	struct boot_fspec_t	kernel;
	struct boot_fspec_t	rd;
	struct boot_fspec_t	sysmap;

	char*	args;
};

extern int useconf;
extern char bootdevice[];
extern char *bootpath;
extern int bootpartition;

#endif
