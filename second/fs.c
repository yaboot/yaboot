/*
 *  fs.c - Filesystem common definitions
 *
 *  Copyright (C) 2001 Ethan Benson
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

#include "stdlib.h"
#include "fs.h"
#include "errors.h"

extern const struct fs_t	of_filesystem;
extern const struct fs_t	of_net_filesystem;
extern const struct fs_t	ext2_filesystem;
//extern const struct fs_t	iso_filesystem;

/* Configurable filesystems */

#ifdef CONFIG_FS_XFS
extern const struct fs_t        xfs_filesystem;
#endif /* CONFIG_FS_XFS */

#ifdef CONFIG_FS_REISERFS
extern const struct fs_t        reiserfs_filesystem;
#endif /* CONFIG_FS_REISERFS */

/* Filesystem handlers yaboot knows about */
static const struct fs_t *block_filesystems[] = {
     &ext2_filesystem,		/* ext2 */
#ifdef CONFIG_FS_XFS
     &xfs_filesystem,                /* XFS */
#endif /* CONFIG_FS_XFS */
#ifdef CONFIG_FS_REISERFS
     &reiserfs_filesystem,		/* reiserfs */
#endif /* CONFIG_FS_REISERFS */
     &of_filesystem,			/* HFS/HFS+, ISO9660, UDF, UFS */
     NULL
};

const struct fs_t *fs_of = &of_filesystem;              /* needed by ISO9660 */
const struct fs_t *fs_of_netboot = &of_net_filesystem;  /* needed by file.c */

const struct fs_t *
fs_open(struct boot_file_t *file, const char *dev_name,
	struct partition_t *part, const char *file_name)
{
     const struct fs_t **fs;
     for (fs = block_filesystems; *fs; fs++)
	  if ((fserrorno = (*fs)->open(file, dev_name, part, file_name)) != FILE_ERR_BAD_FSYS)
	       break;

     return *fs;
}

/* 
 * Local variables:
 * c-file-style: "k&r"
 * c-basic-offset: 5
 * End:
 */
