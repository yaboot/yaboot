/*
 *  fs_swap.c - A filesystem driver to detect swapspace on a partition.
 *
 *  Copyright 2009 Tony Breeds, IBM Corporation
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

#include "ctype.h"
#include "types.h"
#include "stddef.h"
#include "stdlib.h"
#include "file.h"
#include "prom.h"
#include "string.h"
#include "partition.h"
#include "fs.h"
#include "errors.h"
#include "debug.h"
#include "bootinfo.h"

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof(x[0]))
#define BLKCOUNT	2

static struct {
     char *magic;
     int len, offset;
} signatures[] = {
     { "SWAP-SPACE", 10,  0xff6},  /*  4k Pages */
     { "SWAPSPACE2", 10,  0xff6},
     { "SWAP-SPACE", 10, 0xfff6},  /* 64k Pages */
     { "SWAPSPACE2", 10, 0xfff6},
};

/* swap_open: Open a block device and look for swapspace magic.
 *            If we find a valid signature the retuning FILE_ERR_NOTFOUND means
 *            that no other filsystem drivers will check this partition */
static int
swap_open(struct boot_file_t* file, struct partition_t* part,
          struct boot_fspec_t* fspec)
{
     int i;
     unsigned char *buffer;
     /* Make static to move into the BSS rather then the stack */
     static char device_name[1024];

     DEBUG_ENTER;
     DEBUG_OPEN;

     if (file->device_kind != FILE_DEVICE_BLOCK || part == NULL) {
	  DEBUG_LEAVE(FILE_ERR_BADDEV);
	  return FILE_ERR_BADDEV;
     }

     /* We assume that device is "short" and is correctly NULL terminsated */
     strncpy(device_name, fspec->dev, 1020);
     if (_machine != _MACH_bplan)
	  strcat(device_name, ":0");

     DEBUG_F("Opening device: %s\n", device_name);
     file->of_device = prom_open(device_name);
     if (file->of_device == NULL) {
          DEBUG_LEAVE(FILE_IOERR);
          return FILE_IOERR;
     }
     DEBUG_F("file->of_device = %p\n", file->of_device);

     /* If the signature is right on a block boundry we may need two blocks,
      * so lets just allocate room for them now */
     buffer = malloc(sizeof(unsigned char) * part->blocksize * BLKCOUNT);
     if (buffer == NULL) {
          DEBUG_LEAVE("malloc for disk buffer failed\n");
          return FILE_ERR_NOMEM;
     }

     for(i=0; i< ARRAY_SIZE(signatures); i++) {
	  int blk = part->part_start + (signatures[i].offset / part->blocksize);
          int rc __attribute__((unused));
          rc = prom_readblocks(file->of_device, blk, BLKCOUNT, buffer);

          /* FIXME: going past partition length */
          DEBUG_F("Looking for %s @ offset 0x%x, rc == %d, blk=0x%x\n",
                  signatures[i].magic, signatures[i].offset, rc, blk);

          if (memcmp(&buffer[signatures[i].offset % part->blocksize],
                     signatures[i].magic, signatures[i].len) == 0) {
               free(buffer);
               DEBUG_F("Found a swap signature\n");
               DEBUG_LEAVE(FILE_ERR_NOTFOUND);
               return FILE_ERR_NOTFOUND;
          }
     }

     free(buffer);
     prom_close(file->of_device);
     file->of_device = NULL;

     DEBUG_LEAVE(FILE_ERR_BAD_FSYS);
     return FILE_ERR_BAD_FSYS;
}

struct fs_t swap_filesystem =
{
     "swap signature checker",
     swap_open,
     NULL,
     NULL,
     NULL,
};

/*
 * Local variables:
 * c-file-style: "k&r"
 * c-basic-offset: 5
 * End:
 */
