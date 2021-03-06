#include "defs.h"

#include "interrupts.h"
#include "minifile.h"
#include "minifile_cache.h"


/*********************************************************************
 * Semantics of using a block:
 * (1) Create a buf_block_t pointer.
 *     Know the block num to use (from inode, or allocated by balloc).
 * (2) Use bread to get the block.
 * (3) Use bwrite/bdwrite/bawrite/brelse to return the block,
 *     and schedule/immediately write to disk.
 * (+) Before the block is returned, no other thread can use it.
 ********************************************************************/


/* Functions on hash list */
static buf_block_t hash_find(disk_t* disk, blocknum_t n, blocknum_t bhash);
static void hash_add(buf_block_t block, blocknum_t bhash);
static void hash_remove(buf_block_t block, blocknum_t bhash);
static buf_block_t get_buf_block();
static disk_reply_t blocking_read(buf_block_t buf);

/* Initialize buffer cache */
int
minifile_buf_cache_init()
{
    int i;

    disk_lim = semaphore_new(MAX_PENDING_DISK_REQUESTS);
    bc = malloc(sizeof(struct buf_cache));
    if (NULL == disk_lim || NULL == bc)
        goto err1;

    bc->hash_val = BUFFER_CACHE_HASH_VALUE;
    bc->num_blocks = 0;

    bc->locked = queue_new();
    bc->lru = queue_new();
    for (i = 0; i < BUFFER_CACHE_HASH_VALUE; ++i) {
        bc->block_lock[i] = semaphore_new(1);
        bc->block_sig[i] = semaphore_new(0);
    }
    bc->cache_lock = semaphore_new(1);

    if (NULL == bc->locked || NULL == bc->lru || NULL == bc->cache_lock) {
        goto err2;
    }
    for (i = 0; i < BUFFER_CACHE_HASH_VALUE; ++i) {
        if (NULL == bc->block_sig[i] || NULL == bc->block_lock[i]) {
            goto err2;
        }
    }

    return 0;

err2:
    queue_free(bc->locked);
    queue_free(bc->lru);
    semaphore_destroy(bc->cache_lock);
    for (i = 0; i < BUFFER_CACHE_HASH_VALUE; ++i) {
        semaphore_destroy(bc->block_lock[i]);
        semaphore_destroy(bc->block_sig[i]);
    }
err1:
    semaphore_destroy(disk_lim);
    free(bc);
    return -1;
}

static buf_block_t
hash_find(disk_t* disk, blocknum_t n, blocknum_t bhash)
{
    buf_block_t p_block = bc->hash[bhash];
    while (p_block != NULL) {
//        printf("hash_find on block %ld ... looking at block %ld\n", n, p_block->num);
//        if (n == 127 && p_block->num == 0) {
//            printf("hash_prev: %p", p_block->hash_prev);
//            printf("hash_next: %p", p_block->hash_next);
//            p_block = NULL;
//            break;
//        }
        if (p_block->disk == disk && p_block->num == n)
            break;
        p_block = p_block->hash_next;
    }
    return p_block;
}

static void
hash_add(buf_block_t block, blocknum_t bhash)
{
    block->hash_prev = NULL;
    block->hash_next = bc->hash[bhash];
    if (block->hash_next != NULL) {
        block->hash_next->hash_prev = block;
    }
    bc->hash[bhash] = block;

//printf("hash_add on block %ld ... bhash = %ld\n", block->num, bhash);
//printf("hash_prev: %p ", block->hash_prev);
//printf("hash_next: %p\n", block->hash_next);
//printf("bc->hash[bhash]: %p\n", bc->hash[bhash]);
}

static void
hash_remove(buf_block_t block, blocknum_t bhash)
{
    bc->hash[bhash] = block->hash_next;
    if (block->hash_prev != NULL) {
        block->hash_prev->hash_next = block->hash_next;
    }
    if (block->hash_next != NULL) {
        block->hash_next->hash_prev = block->hash_prev;
    }
    block->hash_prev = NULL;
    block->hash_next = NULL;
}

static buf_block_t
get_buf_block()
{
    buf_block_t block = NULL;

    if (bc->num_blocks > bc->hash_val) {
        queue_dequeue(bc->lru, (void**)&block);
    }
    if (NULL == block) {
        block = malloc(sizeof(struct buf_block));
    }
    else {
        if (BLOCK_DIRTY == block->state) {
            blocking_write(block);
        }
        hash_remove(block, BLOCK_NUM_HASH(block->num));
    }

    return block;
}

static disk_reply_t
blocking_read(buf_block_t buf)
{
    interrupt_level_t oldlevel;
    blocknum_t bhash = BLOCK_NUM_HASH(buf->num);

    semaphore_P(disk_lim);
    disk_read_block(buf->disk, buf->num, buf->data);
    oldlevel = set_interrupt_level(DISABLED);
    semaphore_P(bc->block_sig[bhash]);
    set_interrupt_level(oldlevel);
    semaphore_V(disk_lim);
    if (DISK_REPLY_OK == bc->reply[bhash]) {
        buf->state = BLOCK_CLEAN;
    }

    return bc->reply[bhash];
}

disk_reply_t
blocking_write(buf_block_t buf)
{
    interrupt_level_t oldlevel;
    blocknum_t bhash = BLOCK_NUM_HASH(buf->num);

    semaphore_P(disk_lim);
    disk_write_block(buf->disk, buf->num, buf->data);
    oldlevel = set_interrupt_level(DISABLED);
    semaphore_P(bc->block_sig[bhash]);
    set_interrupt_level(oldlevel);
    semaphore_V(disk_lim);
    if (DISK_REPLY_OK == bc->reply[bhash]) {
        buf->state = BLOCK_CLEAN;
    }

    return bc->reply[bhash];
}

/* For block n, get a pointer to its block buffer */
int
bread(disk_t* disk, blocknum_t n, buf_block_t *bufp)
{
    blocknum_t bhash = BLOCK_NUM_HASH(n);

    if (NULL == bufp || NULL == disk || disk->layout.size < n) {
        return -1;
    }

    semaphore_P(bc->block_lock[bhash]);
    semaphore_P(bc->cache_lock);

    *bufp = hash_find(disk, n, bhash);

    if (NULL != *bufp) {
        queue_delete(bc->lru, (void**)bufp);
    } else {
        semaphore_V(bc->cache_lock);

        *bufp = get_buf_block();
        if (NULL == *bufp) {
            return -1;
        }
        (*bufp)->disk = disk;
        (*bufp)->num = n;
        blocking_read(*bufp);

        semaphore_P(bc->cache_lock);

        hash_add(*bufp, bhash);
    }

    semaphore_V(bc->cache_lock);

    return 0;
}


/* Only release the buffer, no write scheduled */
int
brelse(buf_block_t buf)
{
    semaphore_P(bc->cache_lock);
    queue_append(bc->lru, buf);
    semaphore_V(bc->cache_lock);
    semaphore_V(bc->block_lock[BLOCK_NUM_HASH(buf->num)]);
    return 0;
}


/* Immediately write buffer back to disk, block until write finishes */
int
bwrite(buf_block_t buf)
{
    blocking_write(buf);
    brelse(buf);
    return 0;
}


/* Schedule write immediately, but do not block */
void
bawrite(buf_block_t buf)
{
    bdwrite(buf);
}

/* Only mark buffer dirty, no write scheduled */
void
bdwrite(buf_block_t buf)
{
    buf->state = BLOCK_DIRTY;
    brelse(buf);
}

/* 'Pull' data from disk */
int
bpull(blocknum_t from_block, char* to)
{
    buf_block_t block;
    int r = bread(maindisk, from_block, &block);
    memcpy(to, block->data, DISK_BLOCK_SIZE);
    brelse(block);
    return r;
}

/* 'Push' the content of char* onto disk block immediately, blocking */
int
bpush(blocknum_t to_block, char* from)
{
    buf_block_t block;
    bread(maindisk, to_block, &block);
    memcpy(block->data, from, DISK_BLOCK_SIZE);
    return bwrite(block);
}

/* 'Push' the content of char* onto disk block immediately, nonblocking */
int
bapush(blocknum_t to_block, char* from)
{
    buf_block_t block;
    bread(maindisk, to_block, &block);
    memcpy(block->data, from, DISK_BLOCK_SIZE);
    bawrite(block);
    return 0;
}
