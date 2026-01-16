/*! @file cache.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Block cache for a storage device.
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifndef _CACHE_H_
#define _CACHE_H_

#define CACHE_BLKSZ 512UL  // size of cache block

struct storage;  // external
struct cache;    // opaque decl.

extern int create_cache(struct storage* sto, struct cache** cptr);
extern int cache_get_block(struct cache* cache, unsigned long long pos, void** pptr);
extern void cache_release_block(struct cache* cache, void* pblk, int dirty);
extern int cache_flush(struct cache* cache);

#endif  // _CACHE_H_
