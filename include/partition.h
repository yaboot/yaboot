/*
    Partition map management

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

#ifndef PARTITION_H
#define PARTITION_H

struct partition_t;

#include "types.h"
#include "stddef.h"
#include "prom.h"

#define MAX_PARTITIONS	32
#define MAX_PART_NAME	32

struct partition_t {
	struct partition_t*	next;
	int			part_number;
	char			part_name[MAX_PART_NAME];
	unsigned long  		part_start; /* In blocks */
	unsigned long  		part_size; /* In blocks */
	unsigned short		blocksize;
};

extern struct partition_t*	partitions_lookup(const char *device);
extern void			partitions_free(struct partition_t* list);



#endif
