/* File related stuff
   
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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include "ctype.h"
#include "types.h"
#include "stddef.h"
#include "stdlib.h"
#include "file.h"
#include "prom.h"
#include "string.h"
#include "partition.h"
#include "fs.h"

extern char bootdevice[1024];

/* This function follows the device path in the devtree and separates
   the device name, partition number, and other datas (mostly file name)
   the string passed in parameters is changed since 0 are put in place
   of some separators to terminate the various strings
 */
char *
parse_device_path(char *of_device, char **file_spec, int *partition)
{
	char *p, *last;

	if (file_spec)
		*file_spec = NULL;
	if (partition)
		*partition = -1;

	p = strchr(of_device, ':');
	if (p)
		*p = 0;
	else
		return of_device;
	
	last = ++p;
	while(*p && *p != ',') {
        if (!isdigit (*p)) {
			p = last;
			break;
		}
		++p;
	}
	if (p != last) {
		*(p++) = 0;
		if (partition)
            *partition = simple_strtol(last, NULL, 10);
	}
	if (*p && file_spec)
		*file_spec = p;
		
	return of_device;

}

int
validate_fspec(		struct boot_fspec_t*	spec,
			char*			default_device,
			int			default_part)
{
    if (!spec->file) {
    	spec->file = spec->dev;
    	spec->dev = NULL;
    }
    if (spec->part == -1)
    	spec->part = default_part;
    if (!spec->dev)
	spec->dev = default_device;
    if (!spec->file)
	return FILE_BAD_PATH;
    else if (spec->file[0] == ',')
	spec->file++;

    return FILE_ERR_OK;
}

static int
file_block_open(	struct boot_file_t*	file,
			const char*		dev_name,
			const char*		file_name,
			int			partition)
{
	struct partition_t*	parts;
	struct partition_t*	p;
	struct partition_t*	found;
	
	parts = partitions_lookup(dev_name);
	found = NULL;
			
#if DEBUG
	if (parts)
		prom_printf("partitions:\n");
	else
		prom_printf("no partitions found.\n");
#endif
	for (p = parts; p && !found; p=p->next) {
#if DEBUG
		prom_printf("number: %02d, start: 0x%08lx, length: 0x%08lx\n",
			p->part_number, p->part_start, p->part_size );
#endif
		if (partition == -1) {
                        file->fs = fs_open( file, dev_name, p, file_name );
			if (file->fs != NULL)
				goto bail;
		}
		if ((partition >= 0) && (partition == p->part_number))
			found = p;
#if DEBUG
		if (found)
			prom_printf(" (match)\n");
#endif						
	}

	/* Note: we don't skip when found is NULL since we can, in some
	 * cases, let OF figure out a default partition.
	 */
        DEBUG_F( "Using OF defaults.. (found = 0x%x)\n", found );
        file->fs = fs_open( file, dev_name, found, file_name );

bail:
	if (parts)
		partitions_free(parts);

	return file->fs ? FILE_ERR_OK : FILE_ERR_NOTFOUND;
}

static int
file_net_open(	struct boot_file_t*	file,
		const char*		dev_name,
		const char*		file_name)
{
    file->fs = fs_of_netboot;
    return fs_of_netboot->open(file, dev_name, NULL, file_name);
}

static int
default_read(	struct boot_file_t*	file,
		unsigned int		size,
		void*			buffer)
{
	prom_printf("WARNING ! default_read called !\n");
	return FILE_ERR_EOF;
}

static int
default_seek(	struct boot_file_t*	file,
		unsigned int		newpos)
{
	prom_printf("WARNING ! default_seek called !\n");
	return FILE_ERR_EOF;
}

static int
default_close(	struct boot_file_t*	file)
{
	prom_printf("WARNING ! default_close called !\n");
	return FILE_ERR_OK;
}

static struct fs_t fs_default =
{
    "defaults",
    NULL,
    default_read,
    default_seek,
    default_close
};


int open_file(	const struct boot_fspec_t*	spec,
		struct boot_file_t*		file)
{
	static char	temp[1024];
	static char	temps[64];
	char		*dev_name;
	char		*file_name = NULL;
	phandle		dev;
	int		result;
	int		partition;
	
	memset(file, 0, sizeof(struct boot_file_t*));
        file->fs        = &fs_default;

	/* Lookup the OF device path */
	/* First, see if a device was specified for the kernel
	 * if not, we hope that the user wants a kernel on the same
	 * drive and partition as yaboot itself */
	if (!spec->dev)
		strcpy(spec->dev, bootdevice);
	strncpy(temp,spec->dev,1024);
	dev_name = parse_device_path(temp, &file_name, &partition);
	if (file_name == NULL)
		file_name = (char *)spec->file;
	if (file_name == NULL) {
		prom_printf("booting without a file name not yet supported !\n");
		return FILE_ERR_NOTFOUND;
	}
	if (partition == -1)
		partition = spec->part;

#if DEBUG
	prom_printf("dev_path = %s\nfile_name = %s\npartition = %d\n",
		dev_name, file_name, partition);
#endif	
	/* Find OF device phandle */
	dev = prom_finddevice(dev_name);
	if (dev == PROM_INVALID_HANDLE) {
		prom_printf("device not found !\n");
		return FILE_ERR_NOTFOUND;
	}
#if DEBUG
	prom_printf("dev_phandle = %08lx\n", dev);
#endif	
	/* Check the kind of device */
	result = prom_getprop(dev, "device_type", temps, 63);
	if (result == -1) {
		prom_printf("can't get <device_type> for device\n");
		return FILE_ERR_NOTFOUND;
	}
	temps[result] = 0;
	if (!strcmp(temps, "block"))
		file->device_kind = FILE_DEVICE_BLOCK;
	else if (!strcmp(temps, "network"))
		file->device_kind = FILE_DEVICE_NET;
	else {
		prom_printf("Unkown device type <%s>\n", temps);
		return FILE_ERR_NOTFOUND;
	}
	
	switch(file->device_kind) {
	    case FILE_DEVICE_BLOCK:
#if DEBUG
		prom_printf("device is a block device\n");
#endif
	  	return file_block_open(file, dev_name, file_name, partition);
	    case FILE_DEVICE_NET:
#if DEBUG
		prom_printf("device is a network device\n");
#endif
	  	return file_net_open(file, dev_name, file_name);
	}
	return 0;
}
