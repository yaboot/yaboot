#ifndef _REISERFS_H_
#define _REISERFS_H_
#include "byteorder.h"
#include "types.h"

/* ReiserFS Super Block */
/* include/linux/reiserfs_fs_sb.h */
#define REISERFS_MAX_SUPPORTED_VERSION  2
#define REISERFS_SUPER_MAGIC_STRING     "ReIsErFs"
#define REISER2FS_SUPER_MAGIC_STRING    "ReIsEr2Fs"
#define REISERFS_MAX_TREE_HEIGHT        7

struct reiserfs_super_block
{
    __u32 s_block_count;
    __u32 s_free_blocks;            /* free blocks count */
    __u32 s_root_block;             /* root block number */
    __u32 s_journal_block;          /* journal block number */
    __u32 s_journal_dev;            /* journal device number */
    __u32 s_orig_journal_size;      /* size of the journal */
    __u32 s_journal_trans_max;      /* max number of blocks in
                                       a transaction.  */
    __u32 s_journal_block_count;    /* total size of the journal.
                                       can change over time  */
    __u32 s_journal_max_batch;      /* max number of blocks to
                                       batch into a trans */
    __u32 s_journal_max_commit_age; /* in seconds, how old can an
                                       async commit be */
    __u32 s_journal_max_trans_age;  /* in seconds, how old can a
                                       transaction be */
    __u16 s_blocksize;              /* block size */
    __u16 s_oid_maxsize;            /* max size of object id array, */
    __u16 s_oid_cursize;            /* current size of obj id array */
    __u16 s_state;                  /* valid or error */
    char s_magic[12];               /* reiserfs magic string indicates
                                       that file system is reiserfs */
    __u32 s_hash_function_code;	    /* indicate, what hash function is
                                       being use to sort names in a
                                       directory */
    __u16 s_tree_height;            /* height of disk tree */
    __u16 s_bmap_nr;                /* amount of bitmap blocks needed
                                       to address each block of file
                                       system */
    __u16 s_version;
    __u16 s_marked_in_use;
    __u16 s_inode_generation;
    char s_unused[124];             /* zero filled by mkreiserfs */
    char padding_to_quad[ 2 ];      /* aligned to __u32 */
} __attribute__ ((__packed__));
#define SB_SIZE         (sizeof (struct reiserfs_super_block) )

/* ReiserFS Journal */
/* include/linux/reiserfs_fs.h */
/* must be correct to keep the desc and commit structs at 4k */
#define JOURNAL_TRANS_HALF 1018

/* first block written in a commit */
struct reiserfs_journal_desc {
    __u32 j_trans_id;                      /* id of commit */
    __u32 j_len;                           /* length of commit. len +1 is the
                                            commit block */
    __u32 j_mount_id;                      /* mount id of this trans*/
    __u32 j_realblock[JOURNAL_TRANS_HALF]; /* real locations for each block */
  char j_magic[12];
};

/* last block written in a commit */
struct reiserfs_journal_commit {
    __u32 j_trans_id;                      /* must match j_trans_id from the
                                              desc block */
    __u32 j_len;                           /* ditto */
    __u32 j_realblock[JOURNAL_TRANS_HALF]; /* real locations for each block */
    char j_digest[16];                     /* md5 sum of all the blocks
                                              involved, including desc and
                                              commit. not used, kill it */
};

/*
** This header block gets written whenever a transaction is considered
** fully flushed, and is more recent than the last fully flushed
** transaction.  fully flushed means all the log blocks and all the real
** blocks are on disk, and this transaction does not need to be replayed.
*/
struct reiserfs_journal_header {
    __u32 j_last_flush_trans_id;    /* id of last fully flushed transaction */
    __u32 j_first_unflushed_offset; /* offset in the log of where to start
                                       replay after a crash */
    __u32 j_mount_id;
};

/* Magic to find journal descriptors */
#define JOURNAL_DESC_MAGIC "ReIsErLB"

/* ReiserFS Tree structures/accessors */
/* Item version determines which offset_v# struct to use */
#define ITEM_VERSION_1 0
#define ITEM_VERSION_2 1
#define IH_KEY_OFFSET(ih) ((INFO->version < 2 \
                           || ih_version(ih) == ITEM_VERSION_1) \
                           ? le32_to_cpu ((ih)->ih_key.u.k_offset_v1.k_offset) \
                           : offset_v2_k_offset(&(ih)->ih_key.u.k_offset_v2))
 
#define IH_KEY_ISTYPE(ih, type) ((INFO->version < 2 \
                || ih_version(ih) == ITEM_VERSION_1) \
                ? le32_to_cpu((ih)->ih_key.u.k_offset_v1.k_uniqueness) == V1_##type \
                : offset_v2_k_type(&(ih)->ih_key.u.k_offset_v2) == V2_##type)

//
// directories use this key as well as old files
//
struct offset_v1 {
    __u32 k_offset;
    __u32 k_uniqueness;
} __attribute__ ((__packed__));

struct offset_v2 {
#ifdef __LITTLE_ENDIAN
    /* little endian version */
    __u64 k_offset:60;
    __u64 k_type: 4;
#else
    /* big endian version */
    __u64 k_type: 4;
    __u64 k_offset:60;
#endif
} __attribute__ ((__packed__));

#ifndef __LITTLE_ENDIAN
inline __u16 offset_v2_k_type( struct offset_v2 *v2 );
inline loff_t offset_v2_k_offset( struct offset_v2 *v2 );
#else
# define offset_v2_k_type(v2)           ((v2)->k_type)
# define offset_v2_k_offset(v2)         ((v2)->k_offset)
#endif

/* Key of an item determines its location in the S+tree, and
   is composed of 4 components */
struct key {
    __u32 k_dir_id;    /* packing locality: by default parent
			  directory object id */
    __u32 k_objectid;  /* object identifier */
    union {
	struct offset_v1 k_offset_v1;
	struct offset_v2 k_offset_v2;
    } __attribute__ ((__packed__)) u;
} __attribute__ ((__packed__));
#define KEY_SIZE        (sizeof (struct key))

//
// there are 5 item types currently
//
#define TYPE_STAT_DATA 0
#define TYPE_INDIRECT 1
#define TYPE_DIRECT 2
#define TYPE_DIRENTRY 3
#define TYPE_ANY 15 // FIXME: comment is required
 
//
// in old version uniqueness field shows key type
//
#define V1_SD_UNIQUENESS 0
#define V1_INDIRECT_UNIQUENESS 0xfffffffe
#define V1_DIRECT_UNIQUENESS 0xffffffff
#define V1_DIRENTRY_UNIQUENESS 500
#define V1_ANY_UNIQUENESS 555 // FIXME: comment is required
inline int uniqueness2type (__u32 uniqueness);

struct item_head
{
  struct key ih_key; 	              /* Everything in the tree is found by
                                         searching for its key.*/

  union {
    __u16 ih_free_space_reserved;     /* The free space in the last unformatted
                                         node of an indirect item if this is an
                                         indirect item.  This equals 0xFFFF
                                         iff this is a direct item or stat
                                         data item. Note that the key, not
                                         this field, is used to determine
                                         the item type, and thus which field
                                         this union contains. */
    __u16 ih_entry_count;             /* Iff this is a directory item, this
                                         field equals the number of directory
				         entries in the directory item. */
  } __attribute__ ((__packed__)) u;
  __u16 ih_item_len;                  /* total size of the item body */
  __u16 ih_item_location;             /* Offset to the item within the block */
  __u16 ih_version;	              /* ITEM_VERSION_[01] of key type */
} __attribute__ ((__packed__));
#define IH_SIZE (sizeof(struct item_head))

#define ih_version(ih)               le16_to_cpu((ih)->ih_version)
#define ih_entry_count(ih)           le16_to_cpu((ih)->u.ih_entry_count)
#define ih_location(ih)              le16_to_cpu((ih)->ih_item_location)
#define ih_item_len(ih)              le16_to_cpu((ih)->ih_item_len)

/* Header of a disk block.  More precisely, header of a formatted leaf
   or internal node, and not the header of an unformatted node. */
struct block_head {       
    __u16 blk_level;                  /* Level of a block in the tree */
    __u16 blk_nr_item;                /* Number of keys/items in a block */
    __u16 blk_free_space;             /* Block free space in bytes */
    __u16 blk_reserved;
    struct key blk_right_delim_key;   /* kept only for compatibility */
};
#define BLKH_SIZE                     (sizeof(struct block_head))

#define blkh_level(p_blkh)            (le16_to_cpu((p_blkh)->blk_level))
#define blkh_nr_item(p_blkh)          (le16_to_cpu((p_blkh)->blk_nr_item))

#define BLKH_LEVEL_FREE 0 /* Freed from the tree */
#define BLKH_LEVEL_LEAF 1 /* Leaf node level*/

struct disk_child {
    __u32       dc_block_number;   /* Disk child's block number */
    __u16       dc_size;           /* Disk child's used space */
    __u16       dc_reserved;
};

#define DC_SIZE (sizeof(struct disk_child))
#define dc_block_number(dc_p)	(le32_to_cpu((dc_p)->dc_block_number))
#define dc_size(dc_p)		(le16_to_cpu((dc_p)->dc_size))

/* Stat data */
struct stat_data_v1
{
    __u16 sd_mode;              /* file type, permissions */
    __u16 sd_nlink;             /* number of hard links */
    __u16 sd_uid;               /* owner */
    __u16 sd_gid;               /* group */
    __u32 sd_size;	        /* file size */
    __u32 sd_atime;	        /* time of last access */
    __u32 sd_mtime;	        /* time file was last modified  */
    __u32 sd_ctime;	        /* time inode (stat data) was last changed
                                   (except changes to sd_atime and sd_mtime) */
    union {
	__u32 sd_rdev;
	__u32 sd_blocks;	/* number of blocks file uses */
    } __attribute__ ((__packed__)) u;
    __u32 sd_first_direct_byte; /* 0 = no direct item, 1 = symlink */
} __attribute__ ((__packed__));
#define SD_V1_SIZE              (sizeof(struct stat_data_v1))

#define stat_data_v1(ih)        (ih_version (ih) == ITEM_VERSION_1)
#define sd_v1_size(sdp)         (le32_to_cpu((sdp)->sd_size))

/* Stat Data on disk (reiserfs version of UFS disk inode minus the
   address blocks) */
struct stat_data {
    __u16 sd_mode;     /* file type, permissions */
    __u16 sd_reserved;
    __u32 sd_nlink;    /* number of hard links */
    __u64 sd_size;     /* file size */
    __u32 sd_uid;      /* owner */
    __u32 sd_gid;      /* group */
    __u32 sd_atime;    /* time of last access */
    __u32 sd_mtime;    /* time file was last modified  */
    __u32 sd_ctime;    /* time inode (stat data) was last changed
                          (except changes to sd_atime and sd_mtime) */
    __u32 sd_blocks;
    __u32 sd_rdev;
} __attribute__ ((__packed__));
#define SD_V2_SIZE              (sizeof(struct stat_data))
#define stat_data_v2(ih)        (ih_version (ih) == ITEM_VERSION_2)
#define sd_v2_size(sdp)         (le64_to_cpu((sdp)->sd_size))

/* valid for any stat data */
#define sd_size(ih,sdp)         ((ih_version(ih) == ITEM_VERSION_2) ? \
                                  sd_v2_size((struct stat_data *)sdp) : \
                                  sd_v1_size((struct stat_data_v1 *)sdp))
#define sd_mode(sdp)            (le16_to_cpu((sdp)->sd_mode))

struct reiserfs_de_head
{
    __u32 deh_offset;    /* third component of the directory entry key */
    __u32 deh_dir_id;    /* objectid of the parent directory of the object,
                            that is referenced by directory entry */
    __u32 deh_objectid;  /* objectid of the object, that is referenced by
                            directory entry */
    __u16 deh_location;  /* offset of name in the whole item */
    __u16 deh_state;	 /* whether 1) entry contains stat data (for future),                               and 2) whether entry is hidden (unlinked) */
} __attribute__ ((__packed__));
#define DEH_SIZE                  sizeof(struct reiserfs_de_head)

#define deh_offset(p_deh)         (le32_to_cpu((p_deh)->deh_offset))
#define deh_dir_id(p_deh)         (le32_to_cpu((p_deh)->deh_dir_id))
#define deh_objectid(p_deh)       (le32_to_cpu((p_deh)->deh_objectid))
#define deh_location(p_deh)       (le16_to_cpu((p_deh)->deh_location))
#define deh_state(p_deh)          (le16_to_cpu((p_deh)->deh_state))

/* empty directory contains two entries "." and ".." and their headers */
#define EMPTY_DIR_SIZE \
(DEH_SIZE * 2 + ROUND_UP (strlen (".")) + ROUND_UP (strlen ("..")))

/* old format directories have this size when empty */
#define EMPTY_DIR_SIZE_V1 (DEH_SIZE * 2 + 3)

#define DEH_Statdata 0			/* not used now */
#define DEH_Visible 2

/* 64 bit systems need to aligned explicitly -jdm */
#if BITS_PER_LONG == 64
# define ADDR_UNALIGNED_BITS  (5)
#endif

#define test_bit(x,y) ext2fs_test_bit(x,y)

#ifdef ADDR_UNALIGNED_BITS
# define aligned_address(addr)           ((void *)((long)(addr) & ~((1UL << ADDR_UNALIGNED_BITS) - 1)))
# define unaligned_offset(addr)          (((int)((long)(addr) & ((1 << ADDR_UNALIGNED_BITS) - 1))) << 3)
# define set_bit_unaligned(nr, addr)     set_bit((nr) + unaligned_offset(addr), aligned_address(addr))
# define clear_bit_unaligned(nr, addr)   clear_bit((nr) + unaligned_offset(addr), aligned_address(addr))
# define test_bit_unaligned(nr, addr)    test_bit((nr) + unaligned_offset(addr), aligned_address(addr))
#else
# define set_bit_unaligned(nr, addr)     set_bit(nr, addr)
# define clear_bit_unaligned(nr, addr)   clear_bit(nr, addr)
# define test_bit_unaligned(nr, addr)    test_bit(nr, addr)
#endif

#define SD_OFFSET  0
#define SD_UNIQUENESS 0
#define DOT_OFFSET 1
#define DOT_DOT_OFFSET 2
#define DIRENTRY_UNIQUENESS 500
 
#define V1_TYPE_STAT_DATA 0x0
#define V1_TYPE_DIRECT 0xffffffff
#define V1_TYPE_INDIRECT 0xfffffffe
#define V1_TYPE_DIRECTORY_MAX 0xfffffffd
#define V2_TYPE_STAT_DATA 0
#define V2_TYPE_INDIRECT 1
#define V2_TYPE_DIRECT 2
#define V2_TYPE_DIRENTRY 3

 
#define REISERFS_ROOT_OBJECTID 2
#define REISERFS_ROOT_PARENT_OBJECTID 1
#define REISERFS_SUPERBLOCK_BLOCK 16
/* the spot for the super in versions 3.5 - 3.5.11 (inclusive) */
#define REISERFS_OLD_SUPERBLOCK_BLOCK 2
#define REISERFS_OLD_BLOCKSIZE 4096
 
#define S_ISREG(mode) (((mode) & 0170000) == 0100000)
#define S_ISDIR(mode) (((mode) & 0170000) == 0040000)
#define S_ISLNK(mode) (((mode) & 0170000) == 0120000)
#define PATH_MAX       1024     /* include/linux/limits.h */
#define MAX_LINK_COUNT    5     /* number of symbolic links to follow */

/* Cache stuff, adapted from GRUB source */
#define FSYSREISER_CACHE_SIZE        (REISERFS_MAX_TREE_HEIGHT*REISERFS_OLD_BLOCKSIZE)
#define SECTOR_SIZE                  512
#define FSYSREISER_MIN_BLOCKSIZE     SECTOR_SIZE
#define FSYSREISER_MAX_BLOCKSIZE     FSYSREISER_CACHE_SIZE / 3


struct reiserfs_state
{
    /* Context */
    struct key fileinfo;
    struct boot_file_t *file;
    struct item_head *current_ih;
    char *current_item;
    __u64 partition_offset;

    /* Commonly used values, cpu order */
    __u32 journal_block;       /* Start of journal */
    __u32 journal_block_count; /* The size of the journal */
    __u32 journal_first_desc;  /* The first valid descriptor block in journal
                                 (relative to journal_block) */
    
   __u16 version;              /* The ReiserFS version. */
   __u16 tree_depth;           /* The current depth of the reiser tree. */
   __u8  blocksize_shift;      /* 1 << blocksize_shift == blocksize. */
   __u16 blocksize;            /* The reiserfs block size (power of 2) */

    /* Cache */
    __u16 cached_slots;
    __u16 journal_transactions;
    __u32 blocks[REISERFS_MAX_TREE_HEIGHT];
    __u32 next_key_nr[REISERFS_MAX_TREE_HEIGHT];
};

#define ROOT     ((char *)FSYS_BUF)
#define CACHE(i) (ROOT + ((i) * INFO->blocksize))
#define LEAF     CACHE (BLKH_LEVEL_LEAF)

#define BLOCKHEAD(cache) ((struct block_head *) cache)
#define ITEMHEAD         ((struct item_head *) ((int) LEAF + BLKH_SIZE))
#define KEY(cache)       ((struct key *) ((int) cache + BLKH_SIZE))
#define DC(cache)        ((struct disk_child *) \
                                ((int) cache + BLKH_SIZE + KEY_SIZE * nr_item))

/*
 * The journal cache.  For each transaction it contains the number of
 * blocks followed by the real block numbers of this transaction.
 *
 * If the block numbers of some transaction won't fit in this space,
 * this list is stopped with a 0xffffffff marker and the remaining
 * uncommitted transactions aren't cached.
 */
#define JOURNAL_START    ((__u32 *) (FSYS_BUF + FSYSREISER_CACHE_SIZE))
#define JOURNAL_END      ((__u32 *) (FSYS_BUF + sizeof(FSYS_BUF)))


#endif /* _REISERFS_H_ */
