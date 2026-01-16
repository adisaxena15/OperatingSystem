

/*! @file cache.c‌‌‍‍‌‍⁠‌‌‌‌‌⁠‍‌‌⁠‍‌‌‌‍⁠‍‌‌‍⁠‌‌‍‌⁠‍‌‌‌‌‌⁠‍‍‌⁠⁠‌‌‌‌‌‍‌‍‌‍‌‌‍‍⁠⁠‌‍‍‌⁠‌‍‌‍‌‌‌‍‌‍‌‍‌‍‌⁠‌‌‍‍‍‌‍‌⁠⁠‌
    @brief Block cache for a storage device.
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifdef CACHE_TRACE
#define TRACE
#endif

#ifdef CACHE_DEBUG
#define DEBUG
#endif

#include "cache.h"

#include <limits.h>
#include "conf.h"
#include "console.h"
#include "device.h"
#include "devimpl.h"
#include "error.h"
#include "heap.h"
#include "memory.h"
#include "misc.h"
#include "string.h"
#include "thread.h"

// INTERNAL TYPE DEFINITIONS
//

#define CACHE_NUM_BLOCKS 64

struct cache_block {
    char data[CACHE_BLKSZ];
    unsigned long long pos;
    char valid;
    char dirty;
    unsigned int pinned;
    unsigned long long lru_counter;
};
struct cache {
    struct storage* backing;
    struct cache_block* blocks;
    unsigned long long global_counter;
    struct lock cache_lock;
};

// we allocate cache blocks in .bss instead of heap to avoid large heap allocations
static struct cache_block g_cache_blocks[CACHE_NUM_BLOCKS];

static int cache_write_back(struct cache* cache, struct cache_block* block) {
    long result;
    // we validate the block
    if (!block->valid || !block->dirty) {
        return 0;
    }
    // we validate the block is not pinned
    if (block->pinned != 0) {
        return -EBUSY;
    }
    // we write the block back to the storage device
    result = storage_store(cache->backing, block->pos, block->data, CACHE_BLKSZ);
    if (result < 0) {
        return (int)result;
    }
    if (result != CACHE_BLKSZ) {
        return -EIO;
    }

    block->dirty = 0;
    return 0;
}
/**
 * @brief Creates/initializes a cache with the passed backing storage device (disk) and makes it
 * available through cptr.
 * @param disk Pointer to the backing storage device.
 * @param cptr Pointer to the cache to create.
 * @return 0 on success, negative error code if error
 */
// int create_cache(struct storage * disk, struct cache ** cptr)
// Inputs: struct storage * disk - Pointer to the backing storage device
//         struct cache ** cptr - Double pointer to return cache to caller
// Outputs: int - 0 on success, negative error code on failure
// Description: Creates/initializes a cache with the passed backing storage device (disk) and makes it available through cptr.
// Side Effects: Allocates memory for cache structure, initializes all 64 blocks to invalid
int create_cache(struct storage* disk, struct cache** cptr) {
    struct cache* new_cache;
    int i;
    // we validate parameters
    if (disk == NULL || cptr == NULL) {
        return -EINVAL;
    }
    // we allocate cache structure (small, just metadata)
    new_cache = kcalloc(1, sizeof(struct cache));
    if (new_cache == NULL) {
        return -ENOMEM;
    }
    // we initialize cache
    new_cache->backing = disk;
    new_cache->global_counter = 0;
    // we use the static block pool instead of heap allocation
    new_cache->blocks = g_cache_blocks;
    // we now initialize the cache lock
    lock_init(&new_cache->cache_lock);
    // we initialize all blocks to invalid
    for (i = 0; i < CACHE_NUM_BLOCKS; i++) {
        new_cache->blocks[i].valid = 0;
        new_cache->blocks[i].dirty = 0;
        new_cache->blocks[i].pinned = 0;
        new_cache->blocks[i].lru_counter = 0;
        new_cache->blocks[i].pos = 0;
    }
    *cptr = new_cache;
    return 0;
}
/**
 * @brief Reads a CACHE_BLKSZ sized block from the backing interface into the cache.
 * @param cache Pointer to the cache.
 * @param pos Position in the backing storage device. Must be aligned to a multiple of the block
 * size of the backing interface.
 * @param pptr Pointer to the block pointer read from the cache. Assume that CACHE_BLKSZ will always
 * be equal to the block size of the storage disk. Any replacement policy is permitted, as long as
 * your design meets the above specifications.
 * @return 0 on success, negative error code if error
 */
// int cache_get_block(struct cache * cache, unsigned long long pos, void ** pptr)
// Inputs: struct cache * cache - Pointer to the cache
//         unsigned long long pos - Position in the backing storage device
//         void ** pptr - Double pointer to return block pointer to caller
// Outputs: int - 0 on success, negative error code on failure
// Description: Reads a CACHE_BLKSZ sized block from the backing interface into the cache. Checks if block is already in cache (cache hit). If not, finds victim block using LRU policy, writes back if dirty, reads new block from backing storage.
// Side Effects: May evict and write back dirty blocks, reads from backing storage, updates LRU counters
int cache_get_block(struct cache* cache, unsigned long long pos, void** pptr) {
    int i;
    int victim_idx = -1;
    unsigned long long min_lru = ULLONG_MAX;
    long result;
    // We validate parameters
    if (cache == NULL || pptr == NULL) {
        return -EINVAL;
    }
    // we validate position alignment - pos must be block-aligned
    if (pos % CACHE_BLKSZ != 0) {
        return -EINVAL;
    }
    // we now acquire the cache lock for the entire operation
    lock_acquire(&cache->cache_lock);
    // we check if block is already in cache (cache hit)
    for (i = 0; i < CACHE_NUM_BLOCKS; i++) {
        if (cache->blocks[i].valid && cache->blocks[i].pos == pos) {
            // we found the block in cache
            cache->blocks[i].pinned++;
            cache->blocks[i].lru_counter = cache->global_counter++;  
            *pptr = cache->blocks[i].data;
            lock_release(&cache->cache_lock);
            return 0;
        }
    }
    // we cache miss - find a victim block to replace
    // we look for invalid blocks first, then LRU unpinned block
    for (i = 0; i < CACHE_NUM_BLOCKS; i++) {
        if (!cache->blocks[i].valid) {
            victim_idx = i;
            break;
        }
    }
    // we if no invalid blocks, find LRU unpinned block
    if (victim_idx == -1) {
        for (i = 0; i < CACHE_NUM_BLOCKS; i++) {
            if (cache->blocks[i].pinned == 0 && cache->blocks[i].lru_counter < min_lru) {
                min_lru = cache->blocks[i].lru_counter;
                victim_idx = i;
            }
        }
    }
    // we check if we found a victim
    if (victim_idx == -1) {
        lock_release(&cache->cache_lock);
        return -EBUSY;
    }
    // we if victim is dirty, write it back
    if (cache->blocks[victim_idx].valid && cache->blocks[victim_idx].dirty) {
        result = cache_write_back(cache, &cache->blocks[victim_idx]);
        if (result < 0) {
            lock_release(&cache->cache_lock);
            return (int)result;
        }
    }
    // we read new block from backing storage
    result = storage_fetch(cache->backing, pos, cache->blocks[victim_idx].data, CACHE_BLKSZ);
    if (result < 0) {
        lock_release(&cache->cache_lock);
        return result;
    }
    // we check if we got a full block - short reads are not supported by cache
    if (result != CACHE_BLKSZ) {
        lock_release(&cache->cache_lock);
        return -EIO;  // I/O error - incomplete block read
    }
    // we update cache block metadata
    cache->blocks[victim_idx].pos = pos;
    cache->blocks[victim_idx].valid = 1;
    cache->blocks[victim_idx].dirty = 0;
    cache->blocks[victim_idx].pinned = 1;
    cache->blocks[victim_idx].lru_counter = cache->global_counter++;
    *pptr = cache->blocks[victim_idx].data;
    // we now release the cache lock
    lock_release(&cache->cache_lock);
    return 0;
}
/**
 * @brief Releases a block previously obtained from cache_get_block().
 * @param cache Pointer to the cache.
 * @param pblk Pointer to a block that was made available in cache_get_block() (which means that
 * pblk == *pptr for some pptr).
 * @param dirty Indicates whether the block has been modified (1) or not (0). If dirty == 1, the
 * block has been written to. If dirty == 0, the block has not been written to.
 * @return 0 on success, negative error code if error
 */
// void cache_release_block(struct cache * cache, void * pblk, int dirty)
// Inputs: struct cache * cache - Pointer to the cache
//         void * pblk - Pointer to the block to release
//         int dirty - Whether the block has been modified (1) or not (0)
// Outputs: None (void)
// Description: Releases a block previously obtained from cache_get_block(). Unpins the block and updates dirty flag if modified.
// Side Effects: Unpins block, updates dirty flag
void cache_release_block(struct cache* cache, void* pblk, int dirty) {
    int i;
    // We validate parameters
    if (cache == NULL || pblk == NULL) {
        return;
    }
    // we now acquire the cache lock
    lock_acquire(&cache->cache_lock);
    // we find the block by comparing data pointer
    for (i = 0; i < CACHE_NUM_BLOCKS; i++) {
        if (cache->blocks[i].data == pblk) {
            // we found the block
            if (cache->blocks[i].pinned > 0) {
                cache->blocks[i].pinned--;
                if (cache->blocks[i].pinned == 0) {
                    cache->blocks[i].lru_counter = cache->global_counter++;
                }
                if (dirty) {
                    cache->blocks[i].dirty = 1;
                }
            }
            lock_release(&cache->cache_lock);
            return;
        }
    }
    // we now release the cache lock
    lock_release(&cache->cache_lock);
}
/**
 * @brief Flushes the cache to the backing device
 * @param cache Pointer to the cache to flush
 * @return 0 on success, error code if error
 */
// int cache_flush(struct cache * cache)
// Inputs: struct cache * cache - Pointer to the cache to flush
// Outputs: int - 0 on success, negative error code on failure
// Description: Flushes the cache to the backing device by writing all dirty blocks back to backing storage.
// Side Effects: Writes all dirty blocks to backing storage, marks blocks as clean
int cache_flush(struct cache* cache) {
    int i;
    int ret = 0;
    // We validate parameters
    if (cache == NULL) {
        return -EINVAL;
    }
    // we now acquire the cache lock
    lock_acquire(&cache->cache_lock);
    // we write all dirty blocks back to backing storage
    for (i = 0; i < CACHE_NUM_BLOCKS; i++) {
        if (!cache->blocks[i].valid || !cache->blocks[i].dirty) {
            continue;
        }
        if (cache->blocks[i].pinned != 0) {
            ret = ret ? ret : -EBUSY;
            continue;
        }
        {
            int err = cache_write_back(cache, &cache->blocks[i]);
            if (err < 0) {
                lock_release(&cache->cache_lock);
                return err;
            }
        }
    }
    lock_release(&cache->cache_lock);
    return ret;
}
