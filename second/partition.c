/* File related stuff
   
   Copyright (C) 1999 Benjamin Herrenschmidt

   Todo: Add disklabel (or let OF do it ?). Eventually think about
         fixing CDROM handling by directly using the ATAPI layer.
   
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
#include "mac-part.h"
#include "fdisk-part.h"
#include "partition.h"
#include "prom.h"
#include "string.h"
#include "linux/iso_fs.h"
#include "debug.h"
#include "errors.h"

/* We currently don't check the partition type, some users
 * are putting crap there and still expect it to work...
 */
#undef CHECK_FOR_VALID_MAC_PARTITION_TYPE

#ifdef CHECK_FOR_VALID_MAC_PARTITION_TYPE
static const char *valid_mac_partition_types[] = {
     "apple_unix_svr2",
     "linux",
     "apple_hfs",
     "apple_boot",
     "apple_bootstrap",
     NULL
};
#endif
    

/* Local functions */
static unsigned long swab32(unsigned long value);

#define MAX_BLOCK_SIZE	2048
static unsigned char block_buffer[MAX_BLOCK_SIZE];

static void
add_new_partition(struct partition_t**	list, int part_number, const char *part_type,
		  const char *part_name, unsigned long part_start, unsigned long part_size,
		  unsigned short part_blocksize)
{
     struct partition_t*	part;
     part = (struct partition_t*)malloc(sizeof(struct partition_t));
	
     part->part_number = part_number;
     strncpy(part->part_type, part_type, MAX_PART_NAME);
     strncpy(part->part_name, part_name, MAX_PART_NAME);
     part->part_start = part_start;
     part->part_size = part_size;
     part->blocksize = part_blocksize;

     /* Tack this entry onto the list */
     part->next = *list;
     *list = part;
}

/* Note, we rely on partitions being dev-block-size aligned,
 * I have to check if it's true. If it's not, then things will get
 * a bit more complicated
 */
static void
partition_mac_lookup( const char *dev_name, prom_handle disk,
                      unsigned int prom_blksize, struct partition_t** list )
{
     int block, map_size;

     /* block_buffer contains block 0 from the partitions_lookup() stage */
     struct mac_partition* part = (struct mac_partition *)block_buffer;
     unsigned short ptable_block_size =
	  ((struct mac_driver_desc *)block_buffer)->block_size;
	
     map_size = 1;
     for (block=1; block < map_size + 1; block++)
     {
#ifdef CHECK_FOR_VALID_MAC_PARTITION_TYPE
	  int valid = 0;
	  const char *ptype;
#endif
	  if (prom_readblocks(disk, block, 1, block_buffer) != 1) {
	       prom_printf("Can't read partition %d\n", block);
	       break;
	  }
	  if (part->signature != MAC_PARTITION_MAGIC) {
#if 0
	       prom_printf("Wrong partition %d signature\n", block);
#endif
	       break;
	  }
	  if (block == 1)
	       map_size = part->map_count;
		
#ifdef CHECK_FOR_VALID_MAC_PARTITION_TYPE
	  /* We don't bother looking at swap partitions of any type, 
	   * and the rest are the ones we know about */
	  for (ptype = valid_mac_partition_types; ptype; ptype++)
	       if (!strcmp (part->type, ptype))
	       {
		    valid = 1;
		    break;
	       }
#if DEBUG
	  if (!valid)
	       prom_printf( "MAC: Unsupported partition #%d; type=%s\n",
			    block, part->type );
#endif
#endif


#ifdef CHECK_FOR_VALID_MAC_PARTITION_TYPE
	  if (valid)
#endif
	       /* We use the partition block size from the partition table.
		* The filesystem implmentations are responsible for mapping
		* to their own fs blocksize */
	       add_new_partition(
		    list, /* partition list */
		    block, /* partition number */
		    part->type, /* type */
		    part->name, /* name */
		    part->start_block + part->data_start, /* start */
		    part->data_count, /* size */
		    ptable_block_size );
     }
}

/* 
 * Same function as partition_mac_lookup(), except for fdisk
 * partitioned disks.
 */
static void
partition_fdisk_lookup( const char *dev_name, prom_handle disk,
                        unsigned int prom_blksize, struct partition_t** list )
{
     int partition;

     /* fdisk partition tables start at offset 0x1be
      * from byte 0 of the boot drive.
      */
     struct fdisk_partition* part = 
	  (struct fdisk_partition *) (block_buffer + 0x1be);

     for (partition=1; partition <= 4 ;partition++, part++) {
	  if (part->sys_ind == LINUX_NATIVE) {
	       add_new_partition(  list,
				   partition,
				   "Linux", /* type */
				   '\0', /* name */
				   swab32(*(unsigned int *)(part->start4)),
				   swab32(*(unsigned int *)(part->size4)),
				   512 /*blksize*/ );
	  }
     }
}

/* I don't know if it's possible to handle multisession and other multitrack
 * stuffs with the current OF disklabel package. This can still be implemented
 * with direct calls to atapi stuffs.
 * Currently, we enter this code for any device of block size 0x2048 who lacks
 * a MacOS partition map signature.
 */
static int
identify_iso_fs(ihandle device, unsigned int *iso_root_block)
{
     int block;

     for (block = 16; block < 100; block++) {
	  struct iso_volume_descriptor  * vdp;

	  if (prom_readblocks(device, block, 1, block_buffer) != 1) {
	       prom_printf("Can't read volume desc block %d\n", block);
	       break;
	  }
 		
	  vdp = (struct iso_volume_descriptor *)block_buffer;
	    
	  /* Due to the overlapping physical location of the descriptors, 
	   * ISO CDs can match hdp->id==HS_STANDARD_ID as well. To ensure 
	   * proper identification in this case, we first check for ISO.
	   */
	  if (strncmp (vdp->id, ISO_STANDARD_ID, sizeof vdp->id) == 0) {
	       *iso_root_block = block;
	       return 1;
	  }
     }
	
     return 0;
}

struct partition_t*
partitions_lookup(const char *device)
{
     ihandle	disk;
     struct mac_driver_desc *desc = (struct mac_driver_desc *)block_buffer;
     struct partition_t* list = NULL;
     unsigned int prom_blksize, iso_root_block;
	
     strncpy(block_buffer, device, 2040);
     strcat(block_buffer, ":0");
	
     /* Open device */
     disk = prom_open(block_buffer);
     if (disk == NULL) {
	  prom_printf("Can't open device <%s>\n", block_buffer);
	  goto bail;
     }
     prom_blksize = prom_getblksize(disk);
     DEBUG_F("block size of device is %d\n", prom_blksize);

     if (prom_blksize <= 1)
	  prom_blksize = 512;
     if (prom_blksize > MAX_BLOCK_SIZE) {
	  prom_printf("block_size %d not supported !\n", prom_blksize);
	  goto bail;
     }
	
     /* Read boot blocs */
     if (prom_readblocks(disk, 0, 1, block_buffer) != 1) {
	  prom_printf("Can't read boot blocks\n");
	  goto bail;
     }	
     if (desc->signature == MAC_DRIVER_MAGIC) {
	  /* pdisk partition format */
	  partition_mac_lookup(device, disk, prom_blksize, &list);
     } else if ((block_buffer[510] == 0x55) && (block_buffer[511] == 0xaa)) {
	  /* fdisk partition format */
	  partition_fdisk_lookup(device, disk, prom_blksize, &list);
     } else if (prom_blksize == 2048 && identify_iso_fs(disk, &iso_root_block)) {
	  add_new_partition(&list,
			    0,
			    '\0',
			    '\0',
			    iso_root_block,
			    0,
			    prom_blksize);
	  prom_printf("ISO9660 disk\n");
     } else {
	  prom_printf("No supported partition table detected\n");
	  goto bail;
     }

bail:
     prom_close(disk);
	
     return list;
}

char *
get_part_type(char *device, int partition)
{
     struct partition_t*	parts;
     struct partition_t*	p;
     struct partition_t*	found;
     char *type = NULL;

     if (prom_get_devtype(device) != FILE_DEVICE_BLOCK)
	  return NULL;

     parts = partitions_lookup(device);
     found = NULL;

     if (!parts)
	  return '\0';

     for (p = parts; p && !found; p=p->next) {
	  DEBUG_F("number: %02d, start: 0x%08lx, length: 0x%08lx, type: %s, name: %s\n",
		  p->part_number, p->part_start, p->part_size, p->part_type, p->part_name);
	  if ((partition >= 0) && (partition == p->part_number)) {
	       type = strdup(p->part_type);
	       break;
	  }	  
     }
     if (parts)
	  partitions_free(parts);
     return type;
}

/* Freed in reverse order of allocation to help malloc'ator */
void
partitions_free(struct partition_t* list)
{
     struct partition_t*	next;
	
     while(list) {
	  next = list->next;
	  free(list);
	  list = next;
     }
}
unsigned long
swab32(unsigned long value)
{
     __u32 result;

     __asm__("rlwimi %0,%1,24,16,23\n\t"
	     "rlwimi %0,%1,8,8,15\n\t"
	     "rlwimi %0,%1,24,0,7"
	     : "=r" (result)
	     : "r" (value), "0" (value >> 24));
     return result;
}


/* 
 * Local variables:
 * c-file-style: "k&r"
 * c-basic-offset: 5
 * End:
 */
