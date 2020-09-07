/*
 *  fs_reiserfs.c - an implementation for the Reiser filesystem
 *
 *  Copyright (C) 2001 Jeffrey Mahoney (jeffm@suse.com)
 *
 *  Adapted from Grub
 *
 *  Copyright (C) 2000, 2001 Free Software Foundation, Inc.
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
#include "errors.h"
#include "debug.h"
#include "bootinfo.h"
#include "reiserfs/reiserfs.h"

/* Exported in struct fs_t */
static int reiserfs_open( struct boot_file_t *file, struct partition_t *part,
			  struct boot_fspec_t *fspec);
static int reiserfs_read( struct boot_file_t *file, unsigned int size,

			  void *buffer );
static int reiserfs_seek( struct boot_file_t *file, unsigned int newpos );
static int reiserfs_close( struct boot_file_t *file );

struct fs_t reiserfs_filesystem = {
     name:"reiserfs",
     open:reiserfs_open,
     read:reiserfs_read,
     seek:reiserfs_seek,
     close:reiserfs_close
};

static int reiserfs_read_super( void );
static int reiserfs_open_file( char *dirname );
static int reiserfs_read_data( char *buf, __u32 len );


static struct reiserfs_state reiserfs;
static struct reiserfs_state *INFO = &reiserfs;

/* Adapted from GRUB: */
static char FSYS_BUF[FSYSREISER_CACHE_SIZE];
static int errnum;


static int
reiserfs_open( struct boot_file_t *file, struct partition_t *part,
		struct boot_fspec_t *fspec)
{
     static char buffer[1024];
     char *dev_name = fspec->dev;
     char *file_name = fspec->file;

     DEBUG_ENTER;
     DEBUG_OPEN;

     memset( INFO, 0, sizeof(struct reiserfs_state) );
     INFO->file = file;

     if (fspec->part)
     {
	  DEBUG_F( "Determining offset for partition %d\n", part->part_number );
	  INFO->partition_offset = ((uint64_t)part->part_start) * part->blocksize;
	  DEBUG_F( "%Lu = %lu * %hu\n", INFO->partition_offset,
		   part->part_start,
		   part->blocksize );
     }
     else
	  INFO->partition_offset = 0;

     strncpy(buffer, dev_name, 1020);
     if (_machine != _MACH_bplan)
	  strcat(buffer, ":0");  /* 0 is full disk in (non-buggy) OF */

     file->of_device = prom_open( buffer );
     DEBUG_F( "Trying to open dev_name=%s; filename=%s; partition offset=%Lu\n",
	      buffer, file_name, INFO->partition_offset );

     if ( file->of_device == PROM_INVALID_HANDLE || file->of_device == NULL )
     {
	  DEBUG_F( "Can't open device %p\n", file->of_device );
	  DEBUG_LEAVE(FILE_ERR_BADDEV);
	  return FILE_ERR_BADDEV;
     }

     DEBUG_F("%p was successfully opened\n", file->of_device);

     if ( reiserfs_read_super() != 1 )
     {
	  DEBUG_F( "Couldn't open ReiserFS @ %s/%Lu\n", buffer, INFO->partition_offset );
	  prom_close( file->of_device );
	  DEBUG_LEAVE(FILE_ERR_BAD_FSYS);
	  return FILE_ERR_BAD_FSYS;
     }

     DEBUG_F( "Attempting to open %s\n", file_name );
     strcpy(buffer, file_name); /* reiserfs_open_file modifies argument */
     if (reiserfs_open_file(buffer) == 0)
     {
	  DEBUG_F( "reiserfs_open_file failed. errnum = %d\n", errnum );
	  prom_close( file->of_device );
	  DEBUG_LEAVE_F(errnum);
	  return errnum;
     }

     DEBUG_F( "Successfully opened %s\n", file_name );

     DEBUG_LEAVE(FILE_ERR_OK);
     DEBUG_SLEEP;
     return FILE_ERR_OK;
}

static int
reiserfs_read( struct boot_file_t *file, unsigned int size, void *buffer )
{
     return reiserfs_read_data( buffer, size );
}

static int
reiserfs_seek( struct boot_file_t *file, unsigned int newpos )
{
     file->pos = newpos;
     return FILE_ERR_OK;
}

static int
reiserfs_close( struct boot_file_t *file )
{
     if( file->of_device )
     {
	  prom_close(file->of_device);
	  file->of_device = 0;
	  DEBUG_F("reiserfs_close called\n");
     }
     return FILE_ERR_OK;
}


static __inline__ __u32
reiserfs_log2( __u32 word )
{
     int i = 0;
     while( word && (word & (1 << ++i)) == 0 );
     return i;
}

static __inline__ int
is_power_of_two( unsigned long word )
{
     return ( word & -word ) == word;
}

static int
read_disk_block( struct boot_file_t *file, __u32 block, __u32 start,
                 __u32 length, void *buf )
{
     __u16 fs_blocksize = INFO->blocksize == 0 ? REISERFS_OLD_BLOCKSIZE
	  : INFO->blocksize;
     unsigned long long pos = (unsigned long long)block * (unsigned long long)fs_blocksize;
     pos += (unsigned long long)INFO->partition_offset + (unsigned long long)start;
     DEBUG_F( "Reading %u bytes, starting at block %u, disk offset %Lu\n",
	      length, block, pos );
     if (!prom_lseek( file->of_device, pos )) {
	  DEBUG_F("prom_lseek failed\n");
	  return 0;
     }
     return prom_read( file->of_device, buf, length );
}


static int
journal_read( __u32 block, __u32 len, char *buffer )
{
     return read_disk_block( INFO->file,
			     (INFO->journal_block + block), 0,
			     len, buffer );
}

/* Read a block from ReiserFS file system, taking the journal into
 * account.  If the block nr is in the journal, the block from the
 * journal taken.
 */
static int
block_read( __u32 blockNr, __u32 start, __u32 len, char *buffer )
{
     __u32 transactions = INFO->journal_transactions;
     __u32 desc_block = INFO->journal_first_desc;
     __u32 journal_mask = INFO->journal_block_count - 1;
     __u32 translatedNr = blockNr;
     __u32 *journal_table = JOURNAL_START;

//    DEBUG_F( "block_read( %u, %u, %u, ..)\n", blockNr, start, len );

     while ( transactions-- > 0 )
     {
	  int i = 0;
	  int j_len = 0;

	  if ( *journal_table != 0xffffffff )
	  {
	       /* Search for the blockNr in cached journal */
	       j_len = le32_to_cpu((*journal_table)++);
	       while ( i++ < j_len )
	       {
		    if ( le32_to_cpu(*journal_table++) == blockNr )
		    {
			 journal_table += j_len - i;
			 goto found;
		    }
	       }
	  }
	  else
	  {
	       /* This is the end of cached journal marker.  The remaining
		* transactions are still on disk. */
	       struct reiserfs_journal_desc desc;
	       struct reiserfs_journal_commit commit;

	       if ( !journal_read( desc_block, sizeof(desc), (char *) &desc ) )
		    return 0;

	       j_len = le32_to_cpu(desc.j_len);
	       while ( i < j_len && i < JOURNAL_TRANS_HALF )
		    if ( le32_to_cpu(desc.j_realblock[i++]) == blockNr )
			 goto found;

	       if ( j_len >= JOURNAL_TRANS_HALF )
	       {
		    int commit_block = ( desc_block + 1 + j_len ) & journal_mask;

		    if ( !journal_read( commit_block,
					sizeof(commit), (char *) &commit ) )
			 return 0;

		    while ( i < j_len )
			 if ( le32_to_cpu(commit.j_realblock[i++ - JOURNAL_TRANS_HALF]) == blockNr )
			      goto found;
	       }
	  }
	  goto not_found;

     found:
	  translatedNr =
	       INFO->journal_block + ( ( desc_block + i ) & journal_mask );

	  DEBUG_F( "block_read: block %u is mapped to journal block %u.\n",
		   blockNr, translatedNr - INFO->journal_block );

	  /* We must continue the search, as this block may be overwritten in
	   * later transactions. */
     not_found:
	  desc_block = (desc_block + 2 + j_len) & journal_mask;
     }

     return read_disk_block( INFO->file, translatedNr, start, len, buffer );
}

/* Init the journal data structure.  We try to cache as much as
 * possible in the JOURNAL_START-JOURNAL_END space, but if it is full
 * we can still read the rest from the disk on demand.
 *
 * The first number of valid transactions and the descriptor block of the
 * first valid transaction are held in INFO.  The transactions are all
 * adjacent, but we must take care of the journal wrap around.
 */
static int
journal_init( void )
{
     struct reiserfs_journal_header header;
     struct reiserfs_journal_desc desc;
     struct reiserfs_journal_commit commit;
     __u32 block_count = INFO->journal_block_count;
     __u32 desc_block;
     __u32 commit_block;
     __u32 next_trans_id;
     __u32 *journal_table = JOURNAL_START;

     journal_read( block_count, sizeof ( header ), ( char * ) &header );
     desc_block = le32_to_cpu(header.j_first_unflushed_offset);
     if ( desc_block >= block_count )
	  return 0;

     INFO->journal_transactions = 0;
     INFO->journal_first_desc = desc_block;
     next_trans_id = le32_to_cpu(header.j_last_flush_trans_id) + 1;

     DEBUG_F( "journal_init: last flushed %u\n", le32_to_cpu(header.j_last_flush_trans_id) );

     while ( 1 )
     {
	  journal_read( desc_block, sizeof(desc), (char *) &desc );
	  if ( strcmp( JOURNAL_DESC_MAGIC, desc.j_magic ) != 0
	       || desc.j_trans_id != next_trans_id
	       || desc.j_mount_id != header.j_mount_id )
	       /* no more valid transactions */
	       break;

	  commit_block = ( desc_block + le32_to_cpu(desc.j_len) + 1 ) & ( block_count - 1 );
	  journal_read( commit_block, sizeof(commit), (char *) &commit );
	  if ( desc.j_trans_id != commit.j_trans_id
	       || desc.j_len != commit.j_len )
	       /* no more valid transactions */
	       break;


	  DEBUG_F( "Found valid transaction %u/%u at %u.\n",
		   le32_to_cpu(desc.j_trans_id), le32_to_cpu(desc.j_mount_id),
		   desc_block );


	  next_trans_id++;
	  if ( journal_table < JOURNAL_END )
	  {
	       if ( ( journal_table + 1 + le32_to_cpu(desc.j_len) ) >= JOURNAL_END )
	       {
		    /* The table is almost full; mark the end of the cached * *
		     * journal. */
		    *journal_table = 0xffffffff;
		    journal_table = JOURNAL_END;
	       }
	       else
	       {
		    int i;

		    /* Cache the length and the realblock numbers in the table. *
		     * The block number of descriptor can easily be computed. *
		     * and need not to be stored here. */
		    *journal_table++ = desc.j_len;
		    for ( i = 0; i < le32_to_cpu(desc.j_len) && i < JOURNAL_TRANS_HALF; i++ )
		    {
			 *journal_table++ = desc.j_realblock[i];

			 DEBUG_F( "block %u is in journal %u.\n",
				  le32_to_cpu(desc.j_realblock[i]), desc_block );

		    }
		    for ( ; i < le32_to_cpu(desc.j_len); i++ )
		    {
			 *journal_table++ =
			      commit.j_realblock[i - JOURNAL_TRANS_HALF];

			 DEBUG_F( "block %u is in journal %u.\n",
				  le32_to_cpu(commit.j_realblock[i - JOURNAL_TRANS_HALF]),
				  desc_block );

		    }
	       }
	  }
	  desc_block = (commit_block + 1) & (block_count - 1);
     }

     DEBUG_F( "Transaction %u/%u at %u isn't valid.\n",
	      le32_to_cpu(desc.j_trans_id), le32_to_cpu(desc.j_mount_id),
	      desc_block );


     INFO->journal_transactions
	  = next_trans_id - le32_to_cpu(header.j_last_flush_trans_id) - 1;
     return (errnum == 0);
}

/* check filesystem types and read superblock into memory buffer */
static int
reiserfs_read_super( void )
{
     struct reiserfs_super_block super;
     __u64 superblock = REISERFS_SUPERBLOCK_BLOCK;

     if (read_disk_block(INFO->file, superblock, 0, sizeof(super), &super) != sizeof(super)) {
	  DEBUG_F("read_disk_block failed!\n");
	  return 0;
     }

     DEBUG_F( "Found super->magic: \"%s\"\n", super.s_magic );

     if( strcmp( REISER2FS_SUPER_MAGIC_STRING, super.s_magic ) != 0 &&
	 strcmp( REISERFS_SUPER_MAGIC_STRING, super.s_magic ) != 0 )
     {
	  /* Try old super block position */
	  superblock = REISERFS_OLD_SUPERBLOCK_BLOCK;

	  if (read_disk_block( INFO->file, superblock, 0, sizeof (super),  &super ) != sizeof(super)) {
	       DEBUG_F("read_disk_block failed!\n");
	       return 0;
	  }

	  if ( strcmp( REISER2FS_SUPER_MAGIC_STRING, super.s_magic ) != 0 &&
	       strcmp( REISERFS_SUPER_MAGIC_STRING, super.s_magic ) != 0 )
	  {
	       /* pre journaling super block - untested */
	       if ( strcmp( REISERFS_SUPER_MAGIC_STRING,
			    (char *) ((__u32) &super + 20 ) ) != 0 )
		    return 0;

	       super.s_blocksize = cpu_to_le16(REISERFS_OLD_BLOCKSIZE);
	       super.s_journal_block = 0;
	       super.s_version = 0;
	  }
     }

     DEBUG_F( "ReiserFS superblock data:\n" );
     DEBUG_F( "Block count: %u\n", le32_to_cpu(super.s_block_count) )
	  DEBUG_F( "Free blocks: %u\n", le32_to_cpu(super.s_free_blocks) );
     DEBUG_F( "Journal block: %u\n", le32_to_cpu(super.s_journal_block) );
     DEBUG_F( "Journal size (in blocks): %u\n",
	      le32_to_cpu(super.s_orig_journal_size) );
     DEBUG_F( "Root block: %u\n\n", le32_to_cpu(super.s_root_block) );


     INFO->version = le16_to_cpu(super.s_version);
     INFO->blocksize = le16_to_cpu(super.s_blocksize);
     INFO->blocksize_shift = reiserfs_log2( INFO->blocksize );

     INFO->journal_block = le32_to_cpu(super.s_journal_block);
     INFO->journal_block_count = le32_to_cpu(super.s_orig_journal_size);

     INFO->cached_slots = (FSYSREISER_CACHE_SIZE >> INFO->blocksize_shift) - 1;

     /* At this point, we've found a valid superblock. If we run into problems
      * mounting the FS, the user should probably know. */

     /* A few sanity checks ... */
     if ( INFO->version > REISERFS_MAX_SUPPORTED_VERSION )
     {
	  prom_printf( "ReiserFS: Unsupported version field: %u\n",
		       INFO->version );
	  return 0;
     }

     if ( INFO->blocksize < FSYSREISER_MIN_BLOCKSIZE
	  || INFO->blocksize > FSYSREISER_MAX_BLOCKSIZE )
     {
	  prom_printf( "ReiserFS: Unsupported block size: %u\n",
		       INFO->blocksize );
	  return 0;
     }

     /* Setup the journal.. */
     if ( INFO->journal_block != 0 )
     {
	  if ( !is_power_of_two( INFO->journal_block_count ) )
	  {
	       prom_printf( "ReiserFS: Unsupported journal size, "
			    "not a power of 2: %u\n",
			    INFO->journal_block_count );
	       return 0;
	  }

	  journal_init();
	  /* Read in super block again, maybe it is in the journal */
	  block_read( superblock, 0, sizeof (struct reiserfs_super_block),
		      (char *) &super );
     }

     /* Read in the root block */
     if ( !block_read( le32_to_cpu(super.s_root_block), 0,
		       INFO->blocksize, ROOT ) )
     {
	  prom_printf( "ReiserFS: Failed to read in root block\n" );
	  return 0;
     }

     /* The root node is always the "deepest", so we can
	determine the hieght of the tree using it. */
     INFO->tree_depth = blkh_level(BLOCKHEAD(ROOT));


     DEBUG_F( "root read_in: block=%u, depth=%u\n",
	      le32_to_cpu(super.s_root_block), INFO->tree_depth );

     if ( INFO->tree_depth >= REISERFS_MAX_TREE_HEIGHT )
     {
	  prom_printf( "ReiserFS: Unsupported tree depth (too deep): %u\n",
		       INFO->tree_depth );
	  return 0;
     }

     if ( INFO->tree_depth == BLKH_LEVEL_LEAF )
     {
	  /* There is only one node in the whole filesystem, which is
	     simultanously leaf and root */
	  memcpy( LEAF, ROOT, INFO->blocksize );
     }
     return 1;
}

/***************** TREE ACCESSING METHODS *****************************/

/* I assume you are familiar with the ReiserFS tree, if not go to
 * http://devlinux.com/projects/reiserfs/
 *
 * My tree node cache is organized as following
 *   0   ROOT node
 *   1   LEAF node  (if the ROOT is also a LEAF it is copied here
 *   2-n other nodes on current path from bottom to top.
 *       if there is not enough space in the cache, the top most are
 *       omitted.
 *
 * I have only two methods to find a key in the tree:
 *   search_stat(dir_id, objectid) searches for the stat entry (always
 *       the first entry) of an object.
 *   next_key() gets the next key in tree order.
 *
 * This means, that I can only sequential reads of files are
 * efficient, but this really doesn't hurt for grub.
 */

/* Read in the node at the current path and depth into the node cache.
 * You must set INFO->blocks[depth] before.
 */
static char *
read_tree_node( __u32 blockNr, __u16 depth )
{
     char *cache = CACHE(depth);
     int num_cached = INFO->cached_slots;
     errnum = 0;

     if ( depth < num_cached )
     {
	  /* This is the cached part of the path.
	     Check if same block is needed. */
	  if ( blockNr == INFO->blocks[depth] )
	       return cache;
     }
     else
	  cache = CACHE(num_cached);

     DEBUG_F( "  next read_in: block=%u (depth=%u)\n", blockNr, depth );

     if ( !block_read( blockNr, 0, INFO->blocksize, cache ) )
     {
	  DEBUG_F( "block_read failed\n" );
	  return 0;
     }

     DEBUG_F( "FOUND: blk_level=%u, blk_nr_item=%u, blk_free_space=%u\n",
	      blkh_level(BLOCKHEAD(cache)),
	      blkh_nr_item(BLOCKHEAD(cache)),
	      le16_to_cpu(BLOCKHEAD(cache)->blk_free_space) );

     /* Make sure it has the right node level */
     if ( blkh_level(BLOCKHEAD(cache)) != depth )
     {
	  DEBUG_F( "depth = %u != %u\n", blkh_level(BLOCKHEAD(cache)), depth );
	  DEBUG_LEAVE(FILE_ERR_BAD_FSYS);
	  errnum = FILE_ERR_BAD_FSYS;
	  return 0;
     }

     INFO->blocks[depth] = blockNr;
     return cache;
}

/* Get the next key, i.e. the key following the last retrieved key in
 * tree order.  INFO->current_ih and
 * INFO->current_info are adapted accordingly.  */
static int
next_key( void )
{
     __u16 depth;
     struct item_head *ih = INFO->current_ih + 1;
     char *cache;


     DEBUG_F( "next_key:\n  old ih: key %u:%u:%u:%u version:%u\n",
	      le32_to_cpu(INFO->current_ih->ih_key.k_dir_id),
	      le32_to_cpu(INFO->current_ih->ih_key.k_objectid),
	      le32_to_cpu(INFO->current_ih->ih_key.u.k_offset_v1.k_offset),
	      le32_to_cpu(INFO->current_ih->ih_key.u.k_offset_v1.k_uniqueness),
	      ih_version(INFO->current_ih) );


     if ( ih == &ITEMHEAD[blkh_nr_item(BLOCKHEAD( LEAF ))] )
     {
	  depth = BLKH_LEVEL_LEAF;
	  /* The last item, was the last in the leaf node. * Read in the next
	   * * block */
	  do
	  {
	       if ( depth == INFO->tree_depth )
	       {
		    /* There are no more keys at all. * Return a dummy item with
		     * * MAX_KEY */
		    ih =
			 ( struct item_head * )
			 &BLOCKHEAD( LEAF )->blk_right_delim_key;
		    goto found;
	       }
	       depth++;

	       DEBUG_F( "  depth=%u, i=%u\n", depth, INFO->next_key_nr[depth] );

	  }
	  while ( INFO->next_key_nr[depth] == 0 );

	  if ( depth == INFO->tree_depth )
	       cache = ROOT;
	  else if ( depth <= INFO->cached_slots )
	       cache = CACHE( depth );
	  else
	  {
	       /* Save depth as using it twice as args to read_tree_node()
	        * has undefined behaviour */
	       __u16 d = depth;
	       cache = read_tree_node( INFO->blocks[d], --depth );
	       if ( !cache )
		    return 0;
	  }

	  do
	  {
	       __u16 nr_item = blkh_nr_item(BLOCKHEAD( cache ));
	       int key_nr = INFO->next_key_nr[depth]++;


	       DEBUG_F( "  depth=%u, i=%u/%u\n", depth, key_nr, nr_item );

	       if ( key_nr == nr_item )
		    /* This is the last item in this block, set the next_key_nr *
		     * to 0 */
		    INFO->next_key_nr[depth] = 0;

	       cache =
		    read_tree_node( dc_block_number( &(DC( cache )[key_nr])),
				    --depth );
	       if ( !cache )
		    return 0;
	  }
	  while ( depth > BLKH_LEVEL_LEAF );

	  ih = ITEMHEAD;
     }
found:
     INFO->current_ih = ih;
     INFO->current_item = &LEAF[ih_location(ih)];

     DEBUG_F( "  new ih: key %u:%u:%u:%u version:%u\n",
	      le32_to_cpu(INFO->current_ih->ih_key.k_dir_id),
	      le32_to_cpu(INFO->current_ih->ih_key.k_objectid),
	      le32_to_cpu(INFO->current_ih->ih_key.u.k_offset_v1.k_offset),
	      le32_to_cpu(INFO->current_ih->ih_key.u.k_offset_v1.k_uniqueness),
	      ih_version(INFO->current_ih) );

     return 1;
}

/* preconditions: reiserfs_read_super already executed, therefore
 *   INFO block is valid
 * returns: 0 if error (errnum is set),
 *   nonzero iff we were able to find the key successfully.
 * postconditions: on a nonzero return, the current_ih and
 *   current_item fields describe the key that equals the
 *   searched key.  INFO->next_key contains the next key after
 *   the searched key.
 * side effects: messes around with the cache.
 */
static int
search_stat( __u32 dir_id, __u32 objectid )
{
     char *cache;
     int depth;
     int nr_item;
     int i;
     struct item_head *ih;
     errnum = 0;

     DEBUG_F( "search_stat:\n  key %u:%u:0:0\n", le32_to_cpu(dir_id),
	      le32_to_cpu(objectid) );


     depth = INFO->tree_depth;
     cache = ROOT;

     DEBUG_F( "depth = %d\n", depth );
     while ( depth > BLKH_LEVEL_LEAF )
     {
	  struct key *key;

	  nr_item = blkh_nr_item(BLOCKHEAD( cache ));

	  key = KEY( cache );

	  for ( i = 0; i < nr_item; i++ )
	  {
	       if (le32_to_cpu(key->k_dir_id) > le32_to_cpu(dir_id)
		   || (key->k_dir_id == dir_id
		       && (le32_to_cpu(key->k_objectid) > le32_to_cpu(objectid)
			   || (key->k_objectid == objectid
			       && (key->u.k_offset_v1.k_offset
				   | key->u.k_offset_v1.k_uniqueness) > 0))))
		    break;
	       key++;
	  }


	  DEBUG_F( "  depth=%d, i=%d/%d\n", depth, i, nr_item );

	  INFO->next_key_nr[depth] = ( i == nr_item ) ? 0 : i + 1;
	  cache = read_tree_node( dc_block_number(&(DC(cache)[i])), --depth );
	  if ( !cache )
	       return 0;
     }

     /* cache == LEAF */
     nr_item = blkh_nr_item(BLOCKHEAD(LEAF));
     ih = ITEMHEAD;
     DEBUG_F( "nr_item = %d\n", nr_item );
     for ( i = 0; i < nr_item; i++ )
     {
	  if ( ih->ih_key.k_dir_id == dir_id
	       && ih->ih_key.k_objectid == objectid
	       && ih->ih_key.u.k_offset_v1.k_offset == 0
	       && ih->ih_key.u.k_offset_v1.k_uniqueness == 0 )
	  {

	       DEBUG_F( "  depth=%d, i=%d/%d\n", depth, i, nr_item );

	       INFO->current_ih = ih;
	       INFO->current_item = &LEAF[ih_location(ih)];

	       return 1;
	  }

	  ih++;
     }

     DEBUG_LEAVE(FILE_ERR_BAD_FSYS);
     errnum = FILE_ERR_BAD_FSYS;
     return 0;
}

static int
reiserfs_read_data( char *buf, __u32 len )
{
     __u32 blocksize;
     __u32 offset;
     __u32 to_read;
     char *prev_buf = buf;
     errnum = 0;

     DEBUG_F( "reiserfs_read_data: INFO->file->pos=%Lu len=%u, offset=%Lu\n",
	      INFO->file->pos, len, (__u64) IH_KEY_OFFSET(INFO->current_ih) - 1 );


     if ( INFO->current_ih->ih_key.k_objectid != INFO->fileinfo.k_objectid
	  || IH_KEY_OFFSET( INFO->current_ih ) > INFO->file->pos + 1 )
     {
	  search_stat( INFO->fileinfo.k_dir_id, INFO->fileinfo.k_objectid );
	  goto get_next_key;
     }

     while ( errnum == 0 )
     {
	  if ( INFO->current_ih->ih_key.k_objectid != INFO->fileinfo.k_objectid )
	       break;

	  offset = INFO->file->pos - IH_KEY_OFFSET( INFO->current_ih ) + 1;
	  blocksize = ih_item_len(INFO->current_ih);


	  DEBUG_F( "  loop: INFO->file->pos=%Lu len=%u, offset=%u blocksize=%u\n",
		   INFO->file->pos, len, offset, blocksize );


	  if ( IH_KEY_ISTYPE( INFO->current_ih, TYPE_DIRECT )
	       && offset < blocksize )
	  {
	       to_read = blocksize - offset;
	       if ( to_read > len )
		    to_read = len;

	       memcpy( buf, INFO->current_item + offset, to_read );
	       goto update_buf_len;
	  }
	  else if ( IH_KEY_ISTYPE( INFO->current_ih, TYPE_INDIRECT ) )
	  {
	       blocksize = ( blocksize >> 2 ) << INFO->blocksize_shift;

	       while ( offset < blocksize )
	       {
		    __u32 blocknr = le32_to_cpu(((__u32 *)
						 INFO->current_item)[offset >> INFO->blocksize_shift]);

		    int blk_offset = offset & (INFO->blocksize - 1);

		    to_read = INFO->blocksize - blk_offset;
		    if ( to_read > len )
			 to_read = len;

		    /* Journal is only for meta data.
		       Data blocks can be read directly without using block_read */
		    read_disk_block( INFO->file, blocknr, blk_offset, to_read,
				     buf );

	       update_buf_len:
		    len -= to_read;
		    buf += to_read;
		    offset += to_read;
		    INFO->file->pos += to_read;
		    if ( len == 0 )
			 goto done;
	       }
	  }
     get_next_key:
	  next_key();
     }
done:
     return (errnum != 0) ? 0 : buf - prev_buf;
}


/* preconditions: reiserfs_read_super already executed, therefore
 *   INFO block is valid
 * returns: 0 if error, nonzero iff we were able to find the file successfully
 * postconditions: on a nonzero return, INFO->fileinfo contains the info
 *   of the file we were trying to look up, filepos is 0 and filemax is
 *   the size of the file.
 */
static int
reiserfs_open_file( char *dirname )
{
     struct reiserfs_de_head *de_head;
     char *rest, ch;
     __u32 dir_id, objectid, parent_dir_id = 0, parent_objectid = 0;

     char linkbuf[PATH_MAX];	/* buffer for following symbolic links */
     int link_count = 0;
     int mode;
     errnum = 0;

     dir_id = cpu_to_le32(REISERFS_ROOT_PARENT_OBJECTID);
     objectid = cpu_to_le32(REISERFS_ROOT_OBJECTID);

     while ( 1 )
     {

	  DEBUG_F( "dirname=%s\n", dirname );

	  /* Search for the stat info first. */
	  if ( !search_stat( dir_id, objectid ) )
	       return 0;


	  DEBUG_F( "sd_mode=0%o sd_size=%Lu\n",
		   sd_mode((struct stat_data *) INFO->current_item ),
		   sd_size(INFO->current_ih, INFO->current_item ));


	  mode = sd_mode((struct stat_data *)INFO->current_item);

	  /* If we've got a symbolic link, then chase it. */
	  if ( S_ISLNK( mode ) )
	  {
	       int len = 0;

	       DEBUG_F("link count = %d\n", link_count);
	       DEBUG_SLEEP;
	       if ( ++link_count > MAX_LINK_COUNT )
	       {
		    DEBUG_F("Symlink loop\n");
		    errnum = FILE_ERR_SYMLINK_LOOP;
		    return 0;
	       }

	       /* Get the symlink size. */
	       INFO->file->len = sd_size(INFO->current_ih, INFO->current_item);

	       /* Find out how long our remaining name is. */
	       while ( dirname[len] && !isspace( dirname[len] ) )
		    len++;

	       if ( INFO->file->len + len > sizeof ( linkbuf ) - 1 )
	       {
		    errnum = FILE_ERR_LENGTH;
		    return 0;
	       }

	       /* Copy the remaining name to the end of the symlink data. Note *
		* that DIRNAME and LINKBUF may overlap! */
	       memmove( linkbuf + INFO->file->len, dirname, len + 1 );

	       INFO->fileinfo.k_dir_id = dir_id;
	       INFO->fileinfo.k_objectid = objectid;
	       INFO->file->pos = 0;
	       if ( !next_key()
		    || reiserfs_read_data( linkbuf, INFO->file->len ) != INFO->file->len ) {
		    DEBUG_F("reiserfs_open_file - if !next_key || reiserfs_read_data\n");
		    DEBUG_SLEEP;
		    errnum = FILE_IOERR;
		    return 0;
	       }


	       DEBUG_F( "symlink=%s\n", linkbuf );
	       DEBUG_SLEEP;

	       dirname = linkbuf;
	       if ( *dirname == '/' )
	       {
		    /* It's an absolute link, so look it up in root. */
		    dir_id = cpu_to_le32(REISERFS_ROOT_PARENT_OBJECTID);
		    objectid = cpu_to_le32(REISERFS_ROOT_OBJECTID);
	       }
	       else
	       {
		    /* Relative, so look it up in our parent directory. */
		    dir_id = parent_dir_id;
		    objectid = parent_objectid;
	       }

	       /* Now lookup the new name. */
	       continue;
	  }

	  /* if we have a real file (and we're not just printing *
	   * possibilities), then this is where we want to exit */

	  if ( !*dirname || isspace( *dirname ) )
	  {
	       if ( !S_ISREG( mode ) )
	       {
		    errnum = FILE_ERR_BAD_TYPE;
		    return 0;
	       }

	       INFO->file->pos = 0;
	       INFO->file->len = sd_size(INFO->current_ih, INFO->current_item);

	       INFO->fileinfo.k_dir_id = dir_id;
	       INFO->fileinfo.k_objectid = objectid;
	       return next_key();
	  }

	  /* continue with the file/directory name interpretation */
	  while ( *dirname == '/' )
	       dirname++;
	  if ( !S_ISDIR( mode ) )
	  {
	       errnum = FILE_ERR_NOTDIR;
	       return 0;
	  }
	  for ( rest = dirname; ( ch = *rest ) && !isspace( ch ) && ch != '/';
		rest++ ) ;
	  *rest = 0;

	  while ( 1 )
	  {
	       char *name_end;
	       int num_entries;

	       if ( !next_key() )
		    return 0;

	       if ( INFO->current_ih->ih_key.k_objectid != objectid )
		    break;

	       name_end = INFO->current_item + ih_item_len(INFO->current_ih);
	       de_head = ( struct reiserfs_de_head * ) INFO->current_item;
	       num_entries = ih_entry_count(INFO->current_ih);
	       while ( num_entries > 0 )
	       {
		    char *filename = INFO->current_item + deh_location(de_head);
		    char tmp = *name_end;

		    if( deh_state(de_head) & (1 << DEH_Visible))
		    {
			 int cmp;

			 /* Directory names in ReiserFS are not null * terminated.
			  * We write a temporary 0 behind it. * NOTE: that this
			  * may overwrite the first block in * the tree cache.
			  * That doesn't hurt as long as we * don't call next_key
			  * () in between. */
			 *name_end = 0;
			 cmp = strcmp( dirname, filename );
			 *name_end = tmp;
			 if ( cmp == 0 )
			      goto found;
		    }
		    /* The beginning of this name marks the end of the next name.
		     */
		    name_end = filename;
		    de_head++;
		    num_entries--;
	       }
	  }

	  errnum = FILE_ERR_NOTFOUND;
	  *rest = ch;
	  return 0;

     found:
	  *rest = ch;
	  dirname = rest;

	  parent_dir_id = dir_id;
	  parent_objectid = objectid;
	  dir_id = de_head->deh_dir_id; /* LE */
	  objectid = de_head->deh_objectid; /* LE */
     }
}



#ifndef __LITTLE_ENDIAN
typedef union {
     struct offset_v2 offset_v2;
     __u64 linear;
} offset_v2_esafe_overlay;

inline __u16
offset_v2_k_type( struct offset_v2 *v2 )
{
     offset_v2_esafe_overlay tmp = *(offset_v2_esafe_overlay *)v2;
     tmp.linear = le64_to_cpu( tmp.linear );
     return tmp.offset_v2.k_type;
}

inline loff_t
offset_v2_k_offset( struct offset_v2 *v2 )
{
     offset_v2_esafe_overlay tmp = *(offset_v2_esafe_overlay *)v2;
     tmp.linear = le64_to_cpu( tmp.linear );
     return tmp.offset_v2.k_offset;
}
#endif

inline int
uniqueness2type (__u32 uniqueness)
{
     switch (uniqueness) {
     case V1_SD_UNIQUENESS: return TYPE_STAT_DATA;
     case V1_INDIRECT_UNIQUENESS: return TYPE_INDIRECT;
     case V1_DIRECT_UNIQUENESS: return TYPE_DIRECT;
     case V1_DIRENTRY_UNIQUENESS: return TYPE_DIRENTRY;
     }
     return TYPE_ANY;
}

/*
 * Local variables:
 * c-file-style: "k&r"
 * c-basic-offset: 5
 * End:
 */
