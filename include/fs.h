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

#endif
