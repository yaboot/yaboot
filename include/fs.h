/*
    FileSystems common definitions

    Copyright (C) 1999 Benjamin Herrenschmidt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef FS_H
#define FS_H

#include "partition.h"
#include "file.h"

int fserrorno;

struct fs_t {
	const char* name;

	int (*open)(	struct boot_file_t*	file,
			const char*		dev_name,
			struct partition_t*	part,
			const char*		file_name);
			
	int (*read)(	struct boot_file_t*	file,
			unsigned int		size,
			void*			buffer);
				
	int (*seek)(	struct boot_file_t*	file,
			unsigned int		newpos);
					
	int (*close)(	struct boot_file_t*	file);
};

extern const struct fs_t *fs_of;
extern const struct fs_t *fs_of_netboot;

const struct fs_t *fs_open(struct boot_file_t *file, const char *dev_name,
			  struct partition_t *part, const char *file_name);

#if DEBUG
# define DEBUG_ENTER prom_printf( "--> %s\n", __PRETTY_FUNCTION__ )
# define DEBUG_LEAVE(str) \
    prom_printf( "<-- %s - %s\n", __PRETTY_FUNCTION__, #str )
# define DEBUG_LEAVE_F(args...)\
{\
    prom_printf( "<-- %s - %d\n", __PRETTY_FUNCTION__, ## args );\
}
# define DEBUG_F(fmt, args...)\
{\
    prom_printf( "    %s - ", __PRETTY_FUNCTION__ );\
    prom_printf( fmt, ## args );\
}
# define DEBUG_OPEN DEBUG_F( "dev=%s, part=0x%p (%d), file_name=%s\n",\
                             dev_name, part, part ? part->part_number : -1,\
                             file_name)
# define DEBUG_SLEEP prom_sleep(3)
#else
#define DEBUG_ENTER
#define DEBUG_LEAVE(x)
#define DEBUG_LEAVE_F(args...)
#define DEBUG_F(fmt, args...)
#define DEBUG_OPEN
#define DEBUG_SLEEP
#endif

#endif
