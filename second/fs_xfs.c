/*
 *  fs_xfs.c - an implementation for the SGI XFS file system
 *
 *  Copyright (C) 2001, 2002 Ethan Benson
 *
 *  Adapted from Grub
 *
 *  Copyright (C) 2001 Serguei Tzukanov
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

#include "types.h"
#include "ctype.h"
#include "string.h"
#include "stdlib.h"
#include "fs.h"
#include "xfs/xfs.h"
#include "errors.h"
#include "debug.h"
#include "bootinfo.h"

#define SECTOR_BITS 9

int xfs_mount (void);
int xfs_read_data (char *buf, int len);
int xfs_dir (char *dirname);

/* Exported in struct fs_t */
static int xfs_open(struct boot_file_t *file,
		    struct partition_t *part, struct boot_fspec_t *fspec);
static int xfs_read(struct boot_file_t *file, unsigned int size, void *buffer);
static int xfs_seek(struct boot_file_t *file, unsigned int newpos);
static int xfs_close(struct boot_file_t *file);

struct fs_t xfs_filesystem = {
	name:"xfs",
	open:xfs_open,
	read:xfs_read,
	seek:xfs_seek,
	close:xfs_close
};

struct boot_file_t *xfs_file;
static char FSYS_BUF[32768];
uint64_t partition_offset;
static int errnum;

static int
xfs_open(struct boot_file_t *file,
	 struct partition_t *part, struct boot_fspec_t *fspec)
{
	static char buffer[1024];

	DEBUG_ENTER;
	DEBUG_OPEN;

	if (part)
	{
		DEBUG_F("Determining offset for partition %d\n", part->part_number);
		partition_offset = ((uint64_t) part->part_start) * part->blocksize;
		DEBUG_F("%Lu = %lu * %hu\n", partition_offset,
			part->part_start,
			part->blocksize);
	}
	else
		partition_offset = 0;

	strncpy(buffer, fspec->dev, 1020);
	if (_machine != _MACH_bplan)
		strcat(buffer, ":0");  /* 0 is full disk in (non-buggy) OF */
	DEBUG_F("Trying to open dev_name=%s; filename=%s; partition offset=%Lu\n",
		buffer, fspec->file, partition_offset);
	file->of_device = prom_open(buffer);

	if (file->of_device == PROM_INVALID_HANDLE || file->of_device == NULL)
	{
		DEBUG_F("Can't open device %p\n", file->of_device);
		DEBUG_LEAVE(FILE_ERR_BADDEV);
		return FILE_ERR_BADDEV;
	}

	DEBUG_F("%p was successfully opened\n", file->of_device);

	xfs_file = file;

	if (xfs_mount() != 1)
	{
		DEBUG_F("Couldn't open XFS @ %s/%Lu\n", buffer, partition_offset);
		prom_close(file->of_device);
		DEBUG_LEAVE(FILE_ERR_BAD_FSYS);
		DEBUG_SLEEP;
		return FILE_ERR_BAD_FSYS;
	}

	DEBUG_F("Attempting to open %s\n", fspec->file);
	strcpy(buffer, fspec->file); /* xfs_dir modifies argument */
	if(!xfs_dir(buffer))
	{
		DEBUG_F("xfs_dir() failed. errnum = %d\n", errnum);
		prom_close( file->of_device );
		DEBUG_LEAVE_F(errnum);
		DEBUG_SLEEP;
		return errnum;
	}

	DEBUG_F("Successfully opened %s\n", fspec->file);

	DEBUG_LEAVE(FILE_ERR_OK);
	return FILE_ERR_OK;
}

static int
xfs_read(struct boot_file_t *file, unsigned int size, void *buffer)
{
	return xfs_read_data(buffer, size);
}

static int
xfs_seek(struct boot_file_t *file, unsigned int newpos)
{
	file->pos = newpos;
	return FILE_ERR_OK;
}

static int
xfs_close(struct boot_file_t *file)
{
	if(file->of_device)
	{
		prom_close(file->of_device);
		file->of_device = 0;
		DEBUG_F("xfs_close called\n");
	}
	return FILE_ERR_OK;
}

static int
read_disk_block(struct boot_file_t *file, uint64_t block, int start,
		int length, void *buf)
{
	uint64_t pos = block * 512;
	pos += partition_offset + start;
	DEBUG_F("Reading %d bytes, starting at block %Lu, disk offset %Lu\n",
		length, block, pos);
	if (!prom_lseek(file->of_device, pos)) {
		DEBUG_F("prom_lseek failed\n");
		return 0;
	}
	return prom_read(file->of_device, buf, length);
}

#define MAX_LINK_COUNT	8

typedef struct xad {
	xfs_fileoff_t offset;
	xfs_fsblock_t start;
	xfs_filblks_t len;
} xad_t;

struct xfs_info {
	int bsize;
	int dirbsize;
	int isize;
	unsigned int agblocks;
	int bdlog;
	int blklog;
	int inopblog;
	int agblklog;
	int agnolog;
	int dirblklog;
	unsigned int nextents;
	xfs_daddr_t next;
	xfs_daddr_t daddr;
	xfs_dablk_t forw;
	xfs_dablk_t dablk;
	xfs_bmbt_rec_32_t *xt;
	xfs_bmbt_ptr_t ptr0;
	int btnode_ptr0_off;
	int i8param;
	int dirpos;
	int dirmax;
	int blkoff;
	int fpos;
	xfs_ino_t rootino;
};

static struct xfs_info xfs;

#define dirbuf		((char *)FSYS_BUF)
#define filebuf		((char *)FSYS_BUF + 4096)
#define inode		((xfs_dinode_t *)((char *)FSYS_BUF + 8192))
#define icore		(inode->di_core)

#define	mask32lo(n)	(((__uint32_t)1 << (n)) - 1)

#define	XFS_INO_MASK(k)		((__uint32_t)((1ULL << (k)) - 1))
#define	XFS_INO_OFFSET_BITS	xfs.inopblog
#define	XFS_INO_AGBNO_BITS	xfs.agblklog
#define	XFS_INO_AGINO_BITS	(xfs.agblklog + xfs.inopblog)
#define	XFS_INO_AGNO_BITS	xfs.agnolog

static inline xfs_agblock_t
agino2agbno (xfs_agino_t agino)
{
	return agino >> XFS_INO_OFFSET_BITS;
}

static inline xfs_agnumber_t
ino2agno (xfs_ino_t ino)
{
	return ino >> XFS_INO_AGINO_BITS;
}

static inline xfs_agino_t
ino2agino (xfs_ino_t ino)
{
	return ino & XFS_INO_MASK(XFS_INO_AGINO_BITS);
}

static inline int
ino2offset (xfs_ino_t ino)
{
	return ino & XFS_INO_MASK(XFS_INO_OFFSET_BITS);
}

/* XFS is big endian, powerpc is big endian */
#define le16(x) (x)
#define le32(x) (x)
#define le64(x) (x)

static xfs_fsblock_t
xt_start (xfs_bmbt_rec_32_t *r)
{
	return (((xfs_fsblock_t)(le32 (r->l1) & mask32lo(9))) << 43) |
	       (((xfs_fsblock_t)le32 (r->l2)) << 11) |
	       (((xfs_fsblock_t)le32 (r->l3)) >> 21);
}

static xfs_fileoff_t
xt_offset (xfs_bmbt_rec_32_t *r)
{
	return (((xfs_fileoff_t)le32 (r->l0) &
		mask32lo(31)) << 23) |
		(((xfs_fileoff_t)le32 (r->l1)) >> 9);
}

static xfs_filblks_t
xt_len (xfs_bmbt_rec_32_t *r)
{
	return le32(r->l3) & mask32lo(21);
}

static const char xfs_highbit[256] = {
       -1, 0, 1, 1, 2, 2, 2, 2,			/* 00 .. 07 */
	3, 3, 3, 3, 3, 3, 3, 3,			/* 08 .. 0f */
	4, 4, 4, 4, 4, 4, 4, 4,			/* 10 .. 17 */
	4, 4, 4, 4, 4, 4, 4, 4,			/* 18 .. 1f */
	5, 5, 5, 5, 5, 5, 5, 5,			/* 20 .. 27 */
	5, 5, 5, 5, 5, 5, 5, 5,			/* 28 .. 2f */
	5, 5, 5, 5, 5, 5, 5, 5,			/* 30 .. 37 */
	5, 5, 5, 5, 5, 5, 5, 5,			/* 38 .. 3f */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 40 .. 47 */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 48 .. 4f */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 50 .. 57 */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 58 .. 5f */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 60 .. 67 */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 68 .. 6f */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 70 .. 77 */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 78 .. 7f */
	7, 7, 7, 7, 7, 7, 7, 7,			/* 80 .. 87 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* 88 .. 8f */
	7, 7, 7, 7, 7, 7, 7, 7,			/* 90 .. 97 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* 98 .. 9f */
	7, 7, 7, 7, 7, 7, 7, 7,			/* a0 .. a7 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* a8 .. af */
	7, 7, 7, 7, 7, 7, 7, 7,			/* b0 .. b7 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* b8 .. bf */
	7, 7, 7, 7, 7, 7, 7, 7,			/* c0 .. c7 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* c8 .. cf */
	7, 7, 7, 7, 7, 7, 7, 7,			/* d0 .. d7 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* d8 .. df */
	7, 7, 7, 7, 7, 7, 7, 7,			/* e0 .. e7 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* e8 .. ef */
	7, 7, 7, 7, 7, 7, 7, 7,			/* f0 .. f7 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* f8 .. ff */
};

static int
xfs_highbit32(__uint32_t v)
{
	int		i;

	if (v & 0xffff0000)
		if (v & 0xff000000)
			i = 24;
		else
			i = 16;
	else if (v & 0x0000ffff)
		if (v & 0x0000ff00)
			i = 8;
		else
			i = 0;
	else
		return -1;
	return i + xfs_highbit[(v >> i) & 0xff];
}

static int
isinxt (xfs_fileoff_t key, xfs_fileoff_t offset, xfs_filblks_t len)
{
	return (key >= offset) ? (key < offset + len ? 1 : 0) : 0;
}

static xfs_daddr_t
agb2daddr (xfs_agnumber_t agno, xfs_agblock_t agbno)
{
	return ((xfs_fsblock_t)agno*xfs.agblocks + agbno) << xfs.bdlog;
}

static xfs_daddr_t
fsb2daddr (xfs_fsblock_t fsbno)
{
	return agb2daddr ((xfs_agnumber_t)(fsbno >> xfs.agblklog),
			 (xfs_agblock_t)(fsbno & mask32lo(xfs.agblklog)));
}

static inline int
btroot_maxrecs (void)
{
	int tmp = icore.di_forkoff ? (icore.di_forkoff << 3) : xfs.isize;

	return (tmp - sizeof(xfs_bmdr_block_t) -
		(int)((char *)&inode->di_u - (char*)inode)) /
		(sizeof (xfs_bmbt_key_t) + sizeof (xfs_bmbt_ptr_t));
}

static int
di_read (xfs_ino_t ino)
{
	xfs_agino_t agino;
	xfs_agnumber_t agno;
	xfs_agblock_t agbno;
	xfs_daddr_t daddr;
	int offset;

	agno = ino2agno (ino);
	agino = ino2agino (ino);
	agbno = agino2agbno (agino);
	offset = ino2offset (ino);
	daddr = agb2daddr (agno, agbno);

	read_disk_block(xfs_file, daddr, offset*xfs.isize, xfs.isize, (char *)inode);

	xfs.ptr0 = *(xfs_bmbt_ptr_t *)
		    (inode->di_u.di_c + sizeof(xfs_bmdr_block_t)
		    + btroot_maxrecs ()*sizeof(xfs_bmbt_key_t));

	return 1;
}

static void
init_extents (void)
{
	xfs_bmbt_ptr_t ptr0;
	xfs_btree_lblock_t h;

	switch (icore.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		xfs.xt = inode->di_u.di_bmx;
		xfs.nextents = le32 (icore.di_nextents);
		break;
	case XFS_DINODE_FMT_BTREE:
		ptr0 = xfs.ptr0;
		for (;;) {
			xfs.daddr = fsb2daddr (le64(ptr0));
			read_disk_block(xfs_file, xfs.daddr, 0,
					sizeof(xfs_btree_lblock_t), (char *)&h);
			if (!h.bb_level) {
				xfs.nextents = le16(h.bb_numrecs);
				xfs.next = fsb2daddr (le64(h.bb_leftsib));
				xfs.fpos = sizeof(xfs_btree_block_t);
				return;
			}
			read_disk_block(xfs_file, xfs.daddr, xfs.btnode_ptr0_off,
				 sizeof(xfs_bmbt_ptr_t), (char *)&ptr0);
		}
	}
}

static xad_t *
next_extent (void)
{
	static xad_t xad;

	switch (icore.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		if (xfs.nextents == 0)
			return NULL;
		break;
	case XFS_DINODE_FMT_BTREE:
		if (xfs.nextents == 0) {
			xfs_btree_lblock_t h;
			if (xfs.next == 0)
				return NULL;
			xfs.daddr = xfs.next;
			read_disk_block(xfs_file, xfs.daddr, 0,
					sizeof(xfs_btree_lblock_t), (char *)&h);
			xfs.nextents = le16(h.bb_numrecs);
			xfs.next = fsb2daddr (le64(h.bb_leftsib));
			xfs.fpos = sizeof(xfs_btree_block_t);
		}
		/* Yeah, I know that's slow, but I really don't care */
		read_disk_block(xfs_file, xfs.daddr, xfs.fpos,
				sizeof(xfs_bmbt_rec_t), filebuf);
		xfs.xt = (xfs_bmbt_rec_32_t *)filebuf;
		xfs.fpos += sizeof(xfs_bmbt_rec_32_t);
		break;
	default:
		return NULL;
	}
	xad.offset = xt_offset (xfs.xt);
	xad.start = xt_start (xfs.xt);
	xad.len = xt_len (xfs.xt);
	++xfs.xt;
	--xfs.nextents;

	return &xad;
}

/*
 * Name lies - the function reads only first 100 bytes
 */
static void
xfs_dabread (void)
{
	xad_t *xad;
	xfs_fileoff_t offset;;

	init_extents ();
	while ((xad = next_extent ())) {
		offset = xad->offset;
		if (isinxt (xfs.dablk, offset, xad->len)) {
			read_disk_block(xfs_file, fsb2daddr (xad->start + xfs.dablk - offset),
					0, 100, dirbuf);
			break;
		}
	}
}

static inline xfs_ino_t
sf_ino (char *sfe, int namelen)
{
	void *p = sfe + namelen + 3;

	return (xfs.i8param == 0)
		? le64(*(xfs_ino_t *)p) : le32(*(__uint32_t *)p);
}

static inline xfs_ino_t
sf_parent_ino (void)
{
	return (xfs.i8param == 0)
		? le64(*(xfs_ino_t *)(&inode->di_u.di_dir2sf.hdr.parent))
		: le32(*(__uint32_t *)(&inode->di_u.di_dir2sf.hdr.parent));
}

static inline int
roundup8 (int n)
{
	return ((n+7)&~7);
}

static char *
next_dentry (xfs_ino_t *ino)
{
	int namelen = 1;
	int toread;
	static char *usual[2] = {".", ".."};
	static xfs_dir2_sf_entry_t *sfe;
	unsigned char *name = (unsigned char *)usual[0];

	if (xfs.dirpos >= xfs.dirmax) {
		if (xfs.forw == 0)
			return NULL;
		xfs.dablk = xfs.forw;
		xfs_dabread ();
#define h	((xfs_dir2_leaf_hdr_t *)dirbuf)
		xfs.dirmax = le16 (h->count) - le16 (h->stale);
		xfs.forw = le32 (h->info.forw);
#undef h
		xfs.dirpos = 0;
	}

	switch (icore.di_format) {
	case XFS_DINODE_FMT_LOCAL:
		switch (xfs.dirpos) {
		case -2:
			*ino = 0;
			break;
		case -1:
			*ino = sf_parent_ino ();
			++name;
			++namelen;
			sfe = (xfs_dir2_sf_entry_t *)
				(inode->di_u.di_c
				 + sizeof(xfs_dir2_sf_hdr_t)
				 - xfs.i8param);
			break;
		default:
			namelen = sfe->namelen;
			*ino = sf_ino ((char *)sfe, namelen);
			name = sfe->name;
			sfe = (xfs_dir2_sf_entry_t *)
				  ((char *)sfe + namelen + 11 - xfs.i8param);
		}
		break;
	case XFS_DINODE_FMT_BTREE:
	case XFS_DINODE_FMT_EXTENTS:
#define dau	((xfs_dir2_data_union_t *)dirbuf)
		for (;;) {
			if (xfs.blkoff >= xfs.dirbsize) {
				xfs.blkoff = sizeof(xfs_dir2_data_hdr_t);
				xfs_file->pos &= ~(xfs.dirbsize - 1);
				xfs_file->pos |= xfs.blkoff;
			}
			xfs_read_data (dirbuf, 4);
			xfs.blkoff += 4;
			if (dau->unused.freetag == XFS_DIR2_DATA_FREE_TAG) {
				toread = roundup8 (le16(dau->unused.length)) - 4;
				xfs.blkoff += toread;
				xfs_file->pos += toread;
				continue;
			}
			break;
		}
		xfs_read_data ((char *)dirbuf + 4, 5);
		*ino = le64 (dau->entry.inumber);
		namelen = dau->entry.namelen;
#undef dau
		toread = roundup8 (namelen + 11) - 9;
		xfs_read_data (dirbuf, toread);
		name = (unsigned char *)dirbuf;
		xfs.blkoff += toread + 5;
		break;
	}
	++xfs.dirpos;
	name[namelen] = 0;

	return (char *)name;
}

static char *
first_dentry (xfs_ino_t *ino)
{
	xfs.forw = 0;
	switch (icore.di_format) {
	case XFS_DINODE_FMT_LOCAL:
		xfs.dirmax = inode->di_u.di_dir2sf.hdr.count;
		xfs.i8param = inode->di_u.di_dir2sf.hdr.i8count ? 0 : 4;
		xfs.dirpos = -2;
		break;
	case XFS_DINODE_FMT_EXTENTS:
	case XFS_DINODE_FMT_BTREE:
		xfs_file->pos = 0;
		xfs_file->len = le64 (icore.di_size);
		xfs_read_data (dirbuf, sizeof(xfs_dir2_data_hdr_t));
		if (((xfs_dir2_data_hdr_t *)dirbuf)->magic == le32(XFS_DIR2_BLOCK_MAGIC)) {
#define tail		((xfs_dir2_block_tail_t *)dirbuf)
			xfs_file->pos = xfs.dirbsize - sizeof(*tail);
			xfs_read_data (dirbuf, sizeof(*tail));
			xfs.dirmax = le32 (tail->count) - le32 (tail->stale);
#undef tail
		} else {
			xfs.dablk = (1ULL << 35) >> xfs.blklog;
#define h		((xfs_dir2_leaf_hdr_t *)dirbuf)
#define n		((xfs_da_intnode_t *)dirbuf)
			for (;;) {
				xfs_dabread ();
				if ((n->hdr.info.magic == le16(XFS_DIR2_LEAFN_MAGIC))
				    || (n->hdr.info.magic == le16(XFS_DIR2_LEAF1_MAGIC))) {
					xfs.dirmax = le16 (h->count) - le16 (h->stale);
					xfs.forw = le32 (h->info.forw);
					break;
				}
				xfs.dablk = le32 (n->btree[0].before);
			}
#undef n
#undef h
		}
		xfs.blkoff = sizeof(xfs_dir2_data_hdr_t);
		xfs_file->pos = xfs.blkoff;
		xfs.dirpos = 0;
		break;
	}
	return next_dentry (ino);
}

int
xfs_mount (void)
{
	xfs_sb_t super;

	if (read_disk_block(xfs_file, 0, 0, sizeof(super), &super) != sizeof(super)) {
		DEBUG_F("read_disk_block failed!\n");
		return 0;
	} else if (super.sb_magicnum != XFS_SB_MAGIC) {
		DEBUG_F("xfs_mount: Bad magic: %x\n", super.sb_magicnum);
		return 0;
	} else if ((super.sb_versionnum & XFS_SB_VERSION_NUMBITS) != XFS_SB_VERSION_4) {
		DEBUG_F("xfs_mount: Bad version: %x\n", super.sb_versionnum);
		return 0;
	}

	xfs.bsize = le32 (super.sb_blocksize);
	xfs.blklog = super.sb_blocklog;
	xfs.bdlog = xfs.blklog - SECTOR_BITS;
	xfs.rootino = le64 (super.sb_rootino);
	xfs.isize = le16 (super.sb_inodesize);
	xfs.agblocks = le32 (super.sb_agblocks);
	xfs.dirblklog = super.sb_dirblklog;
	xfs.dirbsize = xfs.bsize << super.sb_dirblklog;

	xfs.inopblog = super.sb_inopblog;
	xfs.agblklog = super.sb_agblklog;
	xfs.agnolog = xfs_highbit32 (le32 (super.sb_agcount) - 1) + 1;

	xfs.btnode_ptr0_off =
		((xfs.bsize - sizeof(xfs_btree_block_t)) /
		(sizeof (xfs_bmbt_key_t) + sizeof (xfs_bmbt_ptr_t)))
		 * sizeof(xfs_bmbt_key_t) + sizeof(xfs_btree_block_t);

	return 1;
}

int
xfs_read_data (char *buf, int len)
{
	xad_t *xad;
	xfs_fileoff_t endofprev, endofcur, offset;
	xfs_filblks_t xadlen;
	int toread, startpos, endpos;

	if (icore.di_format == XFS_DINODE_FMT_LOCAL) {
		memmove(buf, inode->di_u.di_c + xfs_file->pos, len);
		xfs_file->pos += len;
		return len;
	}

	startpos = xfs_file->pos;
	endpos = xfs_file->pos + len;
	if (endpos > xfs_file->len)
		endpos = xfs_file->len;
	endofprev = (xfs_fileoff_t)-1;
	init_extents ();
	while (len > 0 && (xad = next_extent ())) {
		offset = xad->offset;
		xadlen = xad->len;
		if (isinxt (xfs_file->pos >> xfs.blklog, offset, xadlen)) {
			endofcur = (offset + xadlen) << xfs.blklog;
			toread = (endofcur >= endpos)
				  ? len : (endofcur - xfs_file->pos);
			read_disk_block(xfs_file, fsb2daddr (xad->start),
					xfs_file->pos - (offset << xfs.blklog), toread, buf);
			buf += toread;
			len -= toread;
			xfs_file->pos += toread;
		} else if (offset > endofprev) {
			toread = ((offset << xfs.blklog) >= endpos)
				  ? len : ((offset - endofprev) << xfs.blklog);
			len -= toread;
			xfs_file->pos += toread;
			for (; toread; toread--) {
				*buf++ = 0;
			}
			continue;
		}
		endofprev = offset + xadlen;
	}

	return xfs_file->pos - startpos;
}

int
xfs_dir (char *dirname)
{
	xfs_ino_t ino, parent_ino, new_ino;
	xfs_fsize_t di_size;
	int di_mode;
	int cmp, n, link_count;
	char linkbuf[xfs.bsize];
	char *rest, *name, ch;

	DEBUG_ENTER;

	parent_ino = ino = xfs.rootino;
	link_count = 0;
	for (;;) {
		di_read (ino);
		di_size = le64 (icore.di_size);
		di_mode = le16 (icore.di_mode);

		DEBUG_F("di_mode: %o\n", di_mode);
		if ((di_mode & IFMT) == IFLNK) {
			if (++link_count > MAX_LINK_COUNT) {
				errnum = FILE_ERR_SYMLINK_LOOP;
				DEBUG_LEAVE(FILE_ERR_SYMLINK_LOOP);
				return 0;
			}
			if (di_size < xfs.bsize - 1) {
				xfs_file->pos = 0;
				xfs_file->len = di_size;
				n = xfs_read_data (linkbuf, xfs_file->len);
			} else {
				errnum = FILE_ERR_LENGTH;
				DEBUG_LEAVE(FILE_ERR_LENGTH);
				return 0;
			}

			ino = (linkbuf[0] == '/') ? xfs.rootino : parent_ino;
			while (n < (xfs.bsize - 1) && (linkbuf[n++] = *dirname++));
			linkbuf[n] = 0;
			dirname = linkbuf;
			continue;
		}

		DEBUG_F("*dirname: %s\n", dirname);
		if (!*dirname || isspace (*dirname)) {
			if ((di_mode & IFMT) != IFREG) {
				errnum = FILE_ERR_BAD_TYPE;
				DEBUG_LEAVE(FILE_ERR_BAD_TYPE);
				return 0;
			}
			xfs_file->pos = 0;
			xfs_file->len = di_size;
			DEBUG_LEAVE(1);
			return 1;
		}

		if ((di_mode & IFMT) != IFDIR) {
			errnum = FILE_ERR_NOTDIR;
			DEBUG_LEAVE(FILE_ERR_NOTDIR);
			return 0;
		}

		for (; *dirname == '/'; dirname++);

		for (rest = dirname; (ch = *rest) && !isspace (ch) && ch != '/'; rest++);
		*rest = 0;

		name = first_dentry (&new_ino);
		for (;;) {
			cmp = (!*dirname) ? -1 : strcmp(dirname, name);
			if (cmp == 0) {
				parent_ino = ino;
				if (new_ino)
					ino = new_ino;
		        	*(dirname = rest) = ch;
				break;
			}
			name = next_dentry (&new_ino);
			if (name == NULL) {
				errnum = FILE_ERR_NOTFOUND;
				DEBUG_LEAVE(FILE_ERR_NOTFOUND);
				*rest = ch;
				return 0;
			}
		}
	}
}

/*
 * Local variables:
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * End:
 */
