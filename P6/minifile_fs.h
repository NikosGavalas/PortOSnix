#ifndef __MINIFILE_FS_H__
#define __MINIFILE_FS_H__

#include "disk.h"
#include "minifile_cache.h"

#define INODE_START_BLOCK 1 /* Inode starts from block 1 */
#define INODE_SIZE 128    /* Size of inode in bytes on disk */
#define INODE_PER_BLOCK (DISK_BLOCK_SIZE / INODE_SIZE)   /* Number of inodes in one block */

/* Translate inode number to block number. Inode starts from 1 which is root directory */
#define INODE_TO_BLOCK(num) ((((num) - 1) / INODE_PER_BLOCK) + INODE_START_BLOCK) 
/* Inode offset within a data block */
#define INODE_OFFSET(num) ((((num) - 1) % INODE_PER_BLOCK) * INODE_SIZE)

/* Address space of inodes */
typedef uint64_t inodenum_t;

/* Types of inodes */
typedef enum inode_type {
    MINIFILE,
    MINIDIRECTORY,
    INODE_EMPTY
} itype_t;

/* Super block on disk */
typedef struct sblock {
    blocknum_t total_blocks;
    blocknum_t total_inodes;
    blocknum_t free_ilist_head;
    blocknum_t free_ilist_tail;
    blocknum_t free_inodes;
    blocknum_t free_blist_head;
    blocknum_t free_blist_tail;
    blocknum_t free_blocks;
    blocknum_t root;
} *sblock_t;

/* Super block in memory */
typedef struct mem_sblock {
    blocknum_t total_blocks;
    blocknum_t total_inodes;
    blocknum_t free_ilist_head;
    blocknum_t free_ilist_tail;
    blocknum_t free_inodes;
    blocknum_t free_blist_head;
    blocknum_t free_blist_tail;
    blocknum_t free_blocks;
    blocknum_t root;

    disk_t* disk;
    blocknum_t pos;
    buf_block_t buf;
} *mem_sblock_t;

/* inode on disk */
typedef struct inode {
        itype_t type;
        size_t size;
        blocknum_t direct[12];
        blocknum_t indirect;
        blocknum_t double_indirect;    /* This support a little more than 1GB file... */
} *inode_t;                            /* 512 * 512 * 4096 / 1024 / 1024 / 1024 = 1GB */

/* indoe in memory */
typedef struct mem_inode {
    itype_t type;
    size_t size_bytes;
    blocknum_t direct[12];
    blocknum_t indirect1;
    blocknum_t indirect2;

    disk_t* disk;
	inodenum_t num;
    buf_block_t buf;
    blocknum_t size_blocks;
	size_t ref_count;
} *mem_inode_t;

/* free block on disk */
typedef struct freeblock {
    blocknum_t next;
} *freeblock_t;

/* Disk space management functions. Explained before implementations. */
extern blocknum_t balloc(disk_t* disk);
extern void bfree(disk_t* disk, blocknum_t n);
extern mem_inode_t ialloc(disk_t* disk);
extern void ifree(disk_t* disk, blocknum_t n);
extern int iclear(disk_t* disk, blocknum_t n);
extern int iget(disk_t* disk, blocknum_t n, mem_inode_t *inop);
extern void iput(mem_inode_t ino);
extern int iupdate(mem_inode_t ino);
extern blocknum_t bmap(size_t byte_offset); /* Byte offset to block number */
#endif /* __MINIFILE_FS_H__ */
