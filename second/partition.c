/*
 *  partition.c - partition table support
 *
 *  Copyright (C) 2004 Sven Luther
 *
 *  Copyright (C) 2001, 2002 Ethan Benson
 *
 *  Copyright (C) 1999 Benjamin Herrenschmidt
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

/*
 *  Todo: Add disklabel (or let OF do it ?). Eventually think about
 *        fixing CDROM handling by directly using the ATAPI layer.
 */

#include "ctype.h"
#include "types.h"
#include "stddef.h"
#include "stdlib.h"
#include "mac-part.h"
#include "fdisk-part.h"
#include "amiga-part.h"
#include "partition.h"
#include "prom.h"
#include "string.h"
#include "linux/iso_fs.h"
#include "debug.h"
#include "errors.h"
#include "bootinfo.h"
#include "byteorder.h"

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


#define MAX_BLOCK_SIZE	2048
static unsigned char block_buffer[MAX_BLOCK_SIZE];

static void
add_new_partition(struct partition_t**	list, int part_number, const char *part_type,
		  const char *part_name, unsigned long part_start, unsigned long part_size,
		  unsigned short part_blocksize, int sys_ind)
{
     struct partition_t*	part;
     part = (struct partition_t*)malloc(sizeof(struct partition_t));

     part->part_number = part_number;
     strncpy(part->part_type, part_type, MAX_PART_NAME);
     strncpy(part->part_name, part_name, MAX_PART_NAME);
     part->part_start = part_start;
     part->part_size = part_size;
     part->blocksize = part_blocksize;
     part->sys_ind = sys_ind;

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
		    ptable_block_size,
		    0);
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
	  if (part->sys_ind == LINUX_NATIVE || part->sys_ind == LINUX_RAID) {
	       add_new_partition(  list,
				   partition,
				   "Linux", /* type */
				   '\0', /* name */
				   le32_to_cpu(*(unsigned int *)part->start4),
				   le32_to_cpu(*(unsigned int *)part->size4),
				   512 /*blksize*/,
				   part->sys_ind /* partition type */ );
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

/*
 * Detects and read amiga partition tables.
 */

static int
_amiga_checksum (unsigned int blk_size)
{
	unsigned int sum;
	int i, end;
	unsigned int *amiga_block = (unsigned int *) block_buffer;

	sum = amiga_block[0];
	end = amiga_block[AMIGA_LENGTH];

	if (end > blk_size) end = blk_size;

	for (i = 1; i < end; i++) sum += amiga_block[i];

	return sum;
}

static int
_amiga_find_rdb (const char *dev_name, prom_handle disk, unsigned int prom_blksize)
{
	int i;
	unsigned int *amiga_block = (unsigned int *) block_buffer;

	for (i = 0; i<AMIGA_RDB_MAX; i++) {
		if (i != 0) {
			if (prom_readblocks(disk, i, 1, block_buffer) != 1) {
	  			prom_printf("Can't read boot block %d\n", i);
	  			break;
			}
		}
		if ((amiga_block[AMIGA_ID] == AMIGA_ID_RDB) && (_amiga_checksum (prom_blksize) == 0))
			return 1;
	}
	/* Amiga partition table not found, let's reread block 0 */
	if (prom_readblocks(disk, 0, 1, block_buffer) != 1) {
  		prom_printf("Can't read boot blocks\n");
  		return 0; /* TODO: something bad happened, should fail more verbosely */
	}
	return 0;
}

static void
partition_amiga_lookup( const char *dev_name, prom_handle disk,
                        unsigned int prom_blksize, struct partition_t** list )
{
	int partition, part;
	unsigned int blockspercyl;
	unsigned int *amiga_block = (unsigned int *) block_buffer;
	unsigned int *used = NULL;
	unsigned int possible;
	int checksum;
	int i;

	blockspercyl = amiga_block[AMIGA_SECT] * amiga_block[AMIGA_HEADS];
	possible = amiga_block[AMIGA_RDBLIMIT]/32 +1;

	used = (unsigned int *) malloc (sizeof (unsigned int) * (possible + 1));
	if (!used) {
               prom_printf("Can't allocate memory\n");
               return;
	}

	for (i=0; i < possible; i++) used[i] = 0;


	for (part = amiga_block[AMIGA_PARTITIONS], partition = 1;
		part != AMIGA_END;
		part = amiga_block[AMIGA_PART_NEXT], partition++)
	{
		if (prom_readblocks(disk, part, 1, block_buffer) != 1) {
	  		prom_printf("Can't read partition block %d\n", part);
	  		break;
		}
		checksum = _amiga_checksum (prom_blksize);
		if ((amiga_block[AMIGA_ID] == AMIGA_ID_PART) &&
			(checksum == 0) &&
			((used[part/32] & (0x1 << (part % 32))) == 0))
		{
			used[part/32] |= (0x1 << (part % 32));
		} else {
	  		prom_printf("Amiga partition table corrupted at block %d\n", part);
			if (amiga_block[AMIGA_ID] != AMIGA_ID_PART)
				prom_printf ("block type is not partition but %08x\n", amiga_block[AMIGA_ID]);
			if (checksum != 0)
				prom_printf ("block checsum is bad : %d\n", checksum);
			if ((used[part/32] & (0x1 << (part % 32))) != 0)
				prom_printf ("partition table is looping, block %d already traveled\n", part);
			break;
		}

	       /* We use the partition block size from the partition table.
		* The filesystem implmentations are responsible for mapping
		* to their own fs blocksize */
	       add_new_partition(
		    list, /* partition list */
		    partition, /* partition number */
		    "Linux", /* type */
		    '\0', /* name */
		    blockspercyl * amiga_block[AMIGA_PART_LOWCYL], /* start */
		    blockspercyl * (amiga_block[AMIGA_PART_HIGHCYL] - amiga_block[AMIGA_PART_LOWCYL] + 1), /* size */
		    prom_blksize,
		    0 );
	}
	if (used)
		free(used);
}

struct partition_t*
partitions_lookup(const char *device)
{
     ihandle	disk;
     struct mac_driver_desc *desc = (struct mac_driver_desc *)block_buffer;
     struct partition_t* list = NULL;
     unsigned int prom_blksize, iso_root_block;

     strncpy((char *)block_buffer, device, 2040);
     if (_machine != _MACH_bplan)
	  strcat((char *)block_buffer, ":0");

     /* Open device */
     disk = prom_open((char *)block_buffer);
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
			    prom_blksize,
			    0);
	  prom_printf("ISO9660 disk\n");
     } else if (_amiga_find_rdb(device, disk, prom_blksize) != -1) {
	  /* amiga partition format */
	  partition_amiga_lookup(device, disk, prom_blksize, &list);
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

     int device_kind = prom_get_devtype(device);
     if (device_kind != FILE_DEVICE_BLOCK && device_kind != FILE_DEVICE_ISCSI)
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


/*
 * Local variables:
 * c-file-style: "k&r"
 * c-basic-offset: 5
 * End:
 */
