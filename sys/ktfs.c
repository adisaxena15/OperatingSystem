

/*! @file ktfs.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief KTFS Implementation.
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifdef KTFS_TRACE
#define TRACE
#endif

#ifdef KTFS_DEBUG
#define DEBUG
#endif

#include "ktfs.h"

#include "cache.h"
#include "console.h"
#include "device.h"
#include "devimpl.h"
#include "error.h"
#include "filesys.h"
#include "fsimpl.h"
#include "heap.h"
#include "misc.h"
#include "string.h"
#include "thread.h"
#include "uio.h"
#include "uioimpl.h"

// INTERNAL TYPE DEFINITIONS
//
struct ktfs_filesystem {
    struct filesystem base;
    struct cache* cache;
    struct ktfs_superblock superblock;
};
/// @brief File struct for a file in the Keegan Teal Filesystem
struct ktfs_file {
    struct uio base;
    struct ktfs_filesystem* fs;
    uint16_t inode_num;
    unsigned long long pos;
    uint32_t file_size;
};
/// @brief Listing struct for iterating through files in KTFS
struct ktfs_listing_uio {
    struct uio base;
    struct ktfs_filesystem* fs;
    uint32_t current_entry;  
    uint32_t total_entries;  
};
// INTERNAL FUNCTION DECLARATIONS
//
int ktfs_open(struct filesystem* fs, const char* name, struct uio** uioptr);
void ktfs_close(struct uio* uio);
int ktfs_cntl(struct uio* uio, int cmd, void* arg);
long ktfs_fetch(struct uio* uio, void* buf, unsigned long len);
long ktfs_store(struct uio* uio, const void* buf, unsigned long len);
int ktfs_create(struct filesystem* fs, const char* name);
int ktfs_delete(struct filesystem* fs, const char* name);
void ktfs_flush(struct filesystem* fs);
void ktfs_listing_close(struct uio* uio);
long ktfs_listing_read(struct uio* uio, void* buf, unsigned long bufsz);

// INTERNAL GLOBAL CONSTANTS
//
static const struct uio_intf ktfs_file_intf = {
    .close = ktfs_close,
    .read = ktfs_fetch,
    .write = ktfs_store,
    .cntl = ktfs_cntl
};
static const struct uio_intf ktfs_listing_intf = {
    .close = ktfs_listing_close,
    .read = ktfs_listing_read
};
static const struct filesystem ktfs_fs_intf = {
    .open = ktfs_open,
    .create = ktfs_create,
    .delete = ktfs_delete,
    .flush = ktfs_flush
};

// HELPER FUNCTIONS
//
static inline uint32_t ktfs_inode_blocks_base(const struct ktfs_filesystem* ktfs) {
    return 1 + ktfs->superblock.inode_bitmap_block_count + ktfs->superblock.bitmap_block_count;
}
static inline uint32_t ktfs_data_blocks_base(const struct ktfs_filesystem* ktfs) {
    return ktfs_inode_blocks_base(ktfs) + ktfs->superblock.inode_block_count;
}
static inline uint32_t ktfs_map_data_num(uint32_t data_base, uint32_t data_num) {
    if (data_num == 0) return 0;
    return data_base + data_num;
}
static int ktfs_name_equals14(const char *name14, const char *want) {
    char tmp[14];
    for (int i = 0; i < 13; i++) tmp[i] = name14[i];
    tmp[13] = '\0';
    return strcmp(tmp, want) == 0;
}
static int ktfs_alloc_data_block(struct ktfs_filesystem* ktfs, uint32_t* dnum_out) {
    if (!ktfs || !dnum_out) return -EINVAL;
    uint32_t bitmap_base       = 1 + ktfs->superblock.inode_bitmap_block_count;
    uint32_t num_bitmap_blocks = ktfs->superblock.bitmap_block_count;
    uint32_t blk_count         = ktfs->superblock.block_count;
    uint32_t data_base         = ktfs_data_blocks_base(ktfs);
    for (uint32_t bmap_blk = 0; bmap_blk < num_bitmap_blocks; bmap_blk++) {
        void* bitmap_block;
        int rc = cache_get_block(ktfs->cache,(unsigned long long)(bitmap_base + bmap_blk) * KTFS_BLKSZ,&bitmap_block);
        if (rc < 0) return rc;
        uint8_t* bitmap = (uint8_t*)bitmap_block;
        for (uint32_t byte_idx = 0; byte_idx < KTFS_BLKSZ; byte_idx++) {
            uint8_t byte = bitmap[byte_idx];
            if (byte == 0xFF) continue;
            for (int bit = 0; bit < 8; bit++) {
                if (byte & (1u << bit)) continue;
                uint32_t bit_index = bmap_blk * (KTFS_BLKSZ * 8) + byte_idx * 8 + (uint32_t)bit;
                if (bit_index >= blk_count) {
                    cache_release_block(ktfs->cache, bitmap_block, 0);
                    return -ENODATABLKS;
                }
                if (bit_index < data_base) {
                    continue;
                }
                bitmap[byte_idx] = (uint8_t)(byte | (1u << bit));
                cache_release_block(ktfs->cache, bitmap_block, 1);
                *dnum_out = bit_index - data_base;
                return 0;
            }
        }
        cache_release_block(ktfs->cache, bitmap_block, 0);
    }
    return -ENODATABLKS;
}
static int ktfs_free_data_block(struct ktfs_filesystem* ktfs, uint32_t dnum) {
    if (!ktfs) return -EINVAL;
    if (dnum == 0) return 0;
    uint32_t data_base = ktfs_data_blocks_base(ktfs);
    uint32_t blk_count = ktfs->superblock.block_count;
    uint32_t blk_index = data_base + dnum;
    if (blk_index >= blk_count) return -EINVAL;
    uint32_t bitmap_base  = 1 + ktfs->superblock.inode_bitmap_block_count;
    uint32_t bit_index    = blk_index;
    uint32_t bmap_blk     = bit_index / (KTFS_BLKSZ * 8);
    uint32_t bit_in_block = bit_index % (KTFS_BLKSZ * 8);
    uint32_t byte_in_block = bit_in_block / 8;
    uint32_t bit_in_byte   = bit_in_block % 8;
    void* bitmap_block;
    int rc = cache_get_block(ktfs->cache,(unsigned long long)(bitmap_base + bmap_blk) * KTFS_BLKSZ,&bitmap_block);
    if (rc < 0) return rc;
    uint8_t* bitmap = (uint8_t*)bitmap_block;
    bitmap[byte_in_block] &= (uint8_t)~(1u << bit_in_byte);
    cache_release_block(ktfs->cache, bitmap_block, 1);
    return 0;
}
static int ktfs_alloc_inode(struct ktfs_filesystem* ktfs, uint16_t* ino_out) {
    // we compute the inode bitmap base and total number of inodes
    uint32_t inode_bitmap_base = 1;
    uint32_t num_inode_bitmap_blocks = ktfs->superblock.inode_bitmap_block_count;
    uint32_t total_inodes = ktfs->superblock.inode_block_count * (KTFS_BLKSZ / KTFS_INOSZ);
    // we iterate through all inode bitmap blocks
    for (uint32_t bmap_blk = 0; bmap_blk < num_inode_bitmap_blocks; bmap_blk++) {
        void* bitmap_block;
        int rc = cache_get_block(ktfs->cache, (unsigned long long)(inode_bitmap_base + bmap_blk) * KTFS_BLKSZ, &bitmap_block);
        if (rc < 0) return rc;
        uint8_t* bitmap = (uint8_t*)bitmap_block;
        // we search for a free inode in this bitmap block
        for (uint32_t byte_idx = 0; byte_idx < KTFS_BLKSZ; byte_idx++) {
            if (bitmap[byte_idx] != 0xFF) {
                for (int bit = 0; bit < 8; bit++) {
                    if (!(bitmap[byte_idx] & (1 << bit))) {
                        // we found a free inode, calculate the inode number
                        uint16_t ino = bmap_blk * KTFS_BLKSZ * 8 + byte_idx * 8 + bit;
                        // we ensure it's not the root directory inode
                        if (ino < total_inodes && ino != ktfs->superblock.root_directory_inode) {
                            // we mark the inode as allocated
                            bitmap[byte_idx] |= (1 << bit);
                            cache_release_block(ktfs->cache, bitmap_block, 1);
                            *ino_out = ino;
                            return 0;
                        }
                    }
                }
            }
        }
        cache_release_block(ktfs->cache, bitmap_block, 0);
    }
    return -ENODATABLKS;
}
static int ktfs_free_inode(struct ktfs_filesystem* ktfs, uint16_t ino) {
    // we compute the bitmap location for this inode
    uint32_t inode_bitmap_base = 1;
    uint32_t bmap_blk = ino / (KTFS_BLKSZ * 8);
    uint32_t byte_in_block = (ino % (KTFS_BLKSZ * 8)) / 8;
    uint32_t bit_in_byte = ino % 8;
    // we get the bitmap block
    void* bitmap_block;
    int rc = cache_get_block(ktfs->cache, (unsigned long long)(inode_bitmap_base + bmap_blk) * KTFS_BLKSZ, &bitmap_block);
    if (rc < 0) return rc;
    // we clear the bit to mark the inode as free
    uint8_t* bitmap = (uint8_t*)bitmap_block;
    bitmap[byte_in_block] &= ~(1 << bit_in_byte);
    cache_release_block(ktfs->cache, bitmap_block, 1);
    return 0;
}
static int ktfs_get_or_alloc_block(struct ktfs_filesystem* ktfs, struct ktfs_inode* inode, uint32_t logical_block, uint32_t* dnum_out, int alloc) {
    // we compute the data base and entries per block
    uint32_t data_base = ktfs_data_blocks_base(ktfs);
    uint32_t entries_per_block = KTFS_BLKSZ / 4;
    // we handle direct blocks
    if (logical_block < KTFS_NUM_DIRECT_DATA_BLOCKS) {
        // we allocate a block if needed and not already allocated
        if (inode->block[logical_block] == 0 && alloc) {
            uint32_t tmp_dnum = 0;
            int rc = ktfs_alloc_data_block(ktfs, &tmp_dnum);
            if (rc < 0) return rc;
            inode->block[logical_block] = tmp_dnum;
        }
        *dnum_out = inode->block[logical_block];
        return 0;
    }
    // we handle indirect blocks
    logical_block -= KTFS_NUM_DIRECT_DATA_BLOCKS;
    if (logical_block < entries_per_block) {
        // we allocate the indirect block if needed
        if (inode->indirect == 0) {
            if (!alloc) {
                *dnum_out = 0;
                return 0;
            }
            uint32_t tmp_dnum = 0;
            int rc = ktfs_alloc_data_block(ktfs, &tmp_dnum);
            if (rc < 0) return rc;
            inode->indirect = tmp_dnum;
            // we initialize the indirect block to zero
            void* indirect_block;
            rc = cache_get_block(ktfs->cache,(unsigned long long)ktfs_map_data_num(data_base, inode->indirect) * KTFS_BLKSZ,&indirect_block);
            if (rc < 0) return rc;
            memset(indirect_block, 0, KTFS_BLKSZ);
            cache_release_block(ktfs->cache, indirect_block, 1);
        }
        // we get the indirect block
        void* indirect_block;
        int rc = cache_get_block(ktfs->cache,(unsigned long long)ktfs_map_data_num(data_base, inode->indirect) * KTFS_BLKSZ, &indirect_block);
        if (rc < 0) return rc;
        uint32_t* indirect_entries = (uint32_t*)indirect_block;
        // we allocate a data block if needed
        uint32_t result_dnum;
        if (indirect_entries[logical_block] == 0 && alloc) {
            rc = ktfs_alloc_data_block(ktfs, &indirect_entries[logical_block]);
            if (rc < 0) {
                cache_release_block(ktfs->cache, indirect_block, 0);
                return rc;
            }
            result_dnum = indirect_entries[logical_block];
            cache_release_block(ktfs->cache, indirect_block, 1);
        } else {
            result_dnum = indirect_entries[logical_block];
            cache_release_block(ktfs->cache, indirect_block, alloc ? 1 : 0);
        }
        
        *dnum_out = result_dnum;
        return 0;
    }
    // we handle doubly-indirect blocks
    logical_block -= entries_per_block;
    uint32_t which_dindirect = logical_block / (entries_per_block * entries_per_block);
    uint32_t remainder = logical_block % (entries_per_block * entries_per_block);
    uint32_t first_level_index = remainder / entries_per_block;
    uint32_t second_level_index = remainder % entries_per_block;
    // we check if the doubly-indirect index is valid
    if (which_dindirect >= KTFS_NUM_DINDIRECT_BLOCKS) {
        *dnum_out = 0;
        return 0;
    }
    // we allocate the doubly-indirect block if needed
    if (inode->dindirect[which_dindirect] == 0) {
        if (!alloc) {
            *dnum_out = 0;
            return 0;
        }
        uint32_t tmp_dnum = 0;
        int rc = ktfs_alloc_data_block(ktfs, &tmp_dnum);
        if (rc < 0) return rc;
        inode->dindirect[which_dindirect] = tmp_dnum;
        // we initialize the doubly-indirect block to zero
        void* double_indirect_block;
        rc = cache_get_block(ktfs->cache,(unsigned long long)ktfs_map_data_num(data_base, inode->dindirect[which_dindirect]) * KTFS_BLKSZ,&double_indirect_block);
        if (rc < 0) return rc;
        memset(double_indirect_block, 0, KTFS_BLKSZ);
        cache_release_block(ktfs->cache, double_indirect_block, 1);
    }
    // we get the doubly-indirect block
    void* double_indirect_block;
    int rc = cache_get_block(ktfs->cache,(unsigned long long)ktfs_map_data_num(data_base, inode->dindirect[which_dindirect]) * KTFS_BLKSZ,&double_indirect_block);
    if (rc < 0) return rc;
    uint32_t* first_level_entries = (uint32_t*)double_indirect_block;
    uint32_t indirect_dnum = first_level_entries[first_level_index];
    // we allocate the indirect block if needed
    if (indirect_dnum == 0) {
        if (!alloc) {
            cache_release_block(ktfs->cache, double_indirect_block, 0);
            *dnum_out = 0;
            return 0;
        }
        rc = ktfs_alloc_data_block(ktfs, &first_level_entries[first_level_index]);
        if (rc < 0) {
            cache_release_block(ktfs->cache, double_indirect_block, 0);
            return rc;
        }
        indirect_dnum = first_level_entries[first_level_index];
        // we initialize the indirect block to zero
        void* indirect_block;
        rc = cache_get_block(ktfs->cache,(unsigned long long)ktfs_map_data_num(data_base, indirect_dnum) * KTFS_BLKSZ,&indirect_block);
        if (rc < 0) {
            cache_release_block(ktfs->cache, double_indirect_block, 1);
            return rc;
        }
        memset(indirect_block, 0, KTFS_BLKSZ);
        cache_release_block(ktfs->cache, indirect_block, 1);
        cache_release_block(ktfs->cache, double_indirect_block, 1);
    } else {
        cache_release_block(ktfs->cache, double_indirect_block, 0);
    }
    // we get the indirect block
    void* indirect_block;
    rc = cache_get_block(ktfs->cache,(unsigned long long)ktfs_map_data_num(data_base, indirect_dnum) * KTFS_BLKSZ,&indirect_block);
    if (rc < 0) return rc;
    uint32_t* indirect_entries = (uint32_t*)indirect_block;
    uint32_t data_dnum = indirect_entries[second_level_index];
    // we allocate the data block if needed
    if (data_dnum == 0 && alloc) {
        rc = ktfs_alloc_data_block(ktfs, &indirect_entries[second_level_index]);
        if (rc < 0) {
            cache_release_block(ktfs->cache, indirect_block, 0);
            return rc;
        }
        data_dnum = indirect_entries[second_level_index];
        cache_release_block(ktfs->cache, indirect_block, 1);
    } else {
        cache_release_block(ktfs->cache, indirect_block, data_dnum == 0 ? 0 : (alloc ? 0 : 0));
    }
    *dnum_out = data_dnum;
    return 0;
}
static int ktfs_free_inode_blocks(struct ktfs_filesystem* ktfs, struct ktfs_inode* inode) {
    // we compute the data base and entries per block
    uint32_t data_base = ktfs_data_blocks_base(ktfs);
    uint32_t entries_per_block = KTFS_BLKSZ / 4;
    // we free all direct data blocks
    for (int i = 0; i < KTFS_NUM_DIRECT_DATA_BLOCKS; i++) {
        if (inode->block[i] != 0) {
            ktfs_free_data_block(ktfs, inode->block[i]);
            inode->block[i] = 0;
        }
    }
    // we free all indirect blocks
    if (inode->indirect != 0) {
        void* indirect_block;
        int rc = cache_get_block(ktfs->cache,(unsigned long long)ktfs_map_data_num(data_base, inode->indirect) * KTFS_BLKSZ,&indirect_block);
        if (rc == 0) {
            uint32_t* indirect_entries = (uint32_t*)indirect_block;
            // we free all data blocks pointed to by the indirect block
            for (uint32_t i = 0; i < entries_per_block; i++) {
                if (indirect_entries[i] != 0) {
                    ktfs_free_data_block(ktfs, indirect_entries[i]);
                }
            }
            cache_release_block(ktfs->cache, indirect_block, 0);
        }
        // we free the indirect block itself
        ktfs_free_data_block(ktfs, inode->indirect);
        inode->indirect = 0;
    }
    // we free all doubly-indirect blocks
    for (int di = 0; di < KTFS_NUM_DINDIRECT_BLOCKS; di++) {
        if (inode->dindirect[di] != 0) {
            void* double_indirect_block;
            int rc = cache_get_block(ktfs->cache,(unsigned long long)ktfs_map_data_num(data_base, inode->dindirect[di]) * KTFS_BLKSZ,&double_indirect_block);
            if (rc == 0) {
                uint32_t* first_level_entries = (uint32_t*)double_indirect_block;
                // we free all indirect blocks pointed to by the doubly-indirect block
                for (uint32_t i = 0; i < entries_per_block; i++) {
                    if (first_level_entries[i] != 0) {
                        void* indirect_block;
                        rc = cache_get_block(ktfs->cache,(unsigned long long)ktfs_map_data_num(data_base, first_level_entries[i]) * KTFS_BLKSZ,&indirect_block);
                        if (rc == 0) {
                            uint32_t* indirect_entries = (uint32_t*)indirect_block;
                            // we free all data blocks pointed to by each indirect block
                            for (uint32_t j = 0; j < entries_per_block; j++) {
                                if (indirect_entries[j] != 0) {
                                    ktfs_free_data_block(ktfs, indirect_entries[j]);
                                }
                            }
                            cache_release_block(ktfs->cache, indirect_block, 0);
                        }
                        // we free the indirect block itself
                        ktfs_free_data_block(ktfs, first_level_entries[i]);
                    }
                }
                cache_release_block(ktfs->cache, double_indirect_block, 0);
            }
            // we free the doubly-indirect block itself
            ktfs_free_data_block(ktfs, inode->dindirect[di]);
            inode->dindirect[di] = 0;
        }
    }
    return 0;
}

/**
 * @brief Mounts the file system with associated backing cache
 * @param cache Pointer to cache struct for the file system
 * @return 0 if mount successful, negative error code if error
 */
int mount_ktfs(const char* name, struct cache* cache) {
    struct ktfs_filesystem* ktfs;
    void* superblock_ptr;
    int result;
    // we validate parameters
    if (!name || !cache) {
        return -EINVAL;
    }
    // we now allocate the filesystem structure
    ktfs = kmalloc(sizeof(struct ktfs_filesystem));
    if (!ktfs) {
        return -ENOMEM;
    }
    // we now initialize the filesystem structure
    ktfs->base = ktfs_fs_intf;
    ktfs->cache = cache;
    // we now read the superblock from block 0
    result = cache_get_block(cache, 0, &superblock_ptr);
    if (result < 0) {
        kfree(ktfs);
        return result;
    }
    // we now copy the superblock to the filesystem structure
    memcpy(&ktfs->superblock, superblock_ptr, sizeof(struct ktfs_superblock));
    // we now release the superblock (not dirty since we only read)
    cache_release_block(cache, superblock_ptr, 0);
    // we now attach the filesystem
    result = attach_filesystem(name, &ktfs->base);
    if (result < 0) {
        kfree(ktfs);
        return result;
    }
    return 0;
}

/**
 * @brief Opens a file or ls (listing) with the given name and returns a pointer to the uio through
 * the double pointer
 * @param name The name of the file to open or "\" for listing (CP3)
 * @param uioptr Will return a pointer to a file or ls (list) uio pointer through this double
 * pointer
 * @return 0 if open successful, negative error code if error
 */
int ktfs_open(struct filesystem* fs, const char* name, struct uio** uioptr) {
    struct ktfs_filesystem* ktfs = (struct ktfs_filesystem*)fs;
    if (!fs || !uioptr) return -EINVAL;
    //we need to check if listing is requested (NULL, empty string, or ".")
    if (name == NULL || *name == '\0' || strcmp(name, ".") == 0) {
        struct ktfs_listing_uio* ls;
        uint16_t root_ino;
        uint32_t ibase, root_blk;
        void* inode_block;
        struct ktfs_inode* root_inode;
        int rc;
        //we need to allocate the listing structure
        ls = kcalloc(1, sizeof(*ls));
        if (!ls) return -ENOMEM;
        //we need to get the root directory information to determine total entries
        root_ino = ktfs->superblock.root_directory_inode;
        ibase = ktfs_inode_blocks_base(ktfs);
        root_blk = ibase + ((root_ino * KTFS_INOSZ) / KTFS_BLKSZ);
        //we need to get the inode block
        rc = cache_get_block(ktfs->cache, (unsigned long long)root_blk * KTFS_BLKSZ, &inode_block);
        if (rc < 0) {
            kfree(ls);
            return rc;
        }
        //we need to get the offset
        uint32_t off = (root_ino * KTFS_INOSZ) % KTFS_BLKSZ;
        root_inode = (struct ktfs_inode*)((uint8_t*)inode_block + off);
        uint32_t root_dir_size = root_inode->size;
        cache_release_block(ktfs->cache, inode_block, 0);
        //we need to initialize the listing structure
        ls->fs = ktfs;
        ls->current_entry = 0;
        ls->total_entries = root_dir_size / KTFS_DENSZ;
        //we need to initialize the uio
        *uioptr = uio_init1(&ls->base, &ktfs_listing_intf);
        return 0;
    }
    //we need to handle regular file open - name must be valid
    if (!name) return -EINVAL;
    // we compute the bases
    uint32_t ibase = ktfs_inode_blocks_base(ktfs);
    uint32_t data_base = ktfs_data_blocks_base(ktfs);
    // we fetch the root inode
    uint16_t root_ino = ktfs->superblock.root_directory_inode;
    uint32_t root_blk = ibase + ((root_ino * KTFS_INOSZ) / KTFS_BLKSZ);
    void *inode_block;
    int rc = cache_get_block(ktfs->cache, (unsigned long long)root_blk * KTFS_BLKSZ, &inode_block);
    if (rc < 0) return rc;
    uint32_t off = (root_ino * KTFS_INOSZ) % KTFS_BLKSZ;
    struct ktfs_inode *root_inode = (struct ktfs_inode*)((uint8_t*)inode_block + off);
    uint32_t root_dir_size = root_inode->size;
    uint32_t num_entries = root_dir_size / KTFS_DENSZ;
    // we detect the "contiguous root" layout (all pointers zero but size > 0)
    int root_has_no_ptrs = (root_inode->block[0] | root_inode->block[1] | root_inode->block[2] | root_inode->block[3] |root_inode->indirect) == 0;
    // we check if the root has no pointers and the number of entries is greater than 0
    if (root_has_no_ptrs && num_entries > 0) {
        // we fallback: scan contiguous dir blocks starting at data_base
        uint32_t need_blocks = (root_dir_size + KTFS_BLKSZ - 1) / KTFS_BLKSZ;
        uint32_t dentries_checked = 0;
        for (uint32_t b = 0; b < need_blocks && dentries_checked < num_entries; b++) {
            void* root_block = NULL;
            rc = cache_get_block(ktfs->cache,(unsigned long long)(data_base + b) * KTFS_BLKSZ,&root_block);
            if (rc < 0) { cache_release_block(ktfs->cache, inode_block, 0); return rc; }
            // we calculate the maximum number of slots in a block
            uint32_t slot_max = KTFS_BLKSZ / KTFS_DENSZ;
            uint32_t here = (num_entries - dentries_checked < slot_max)
                                ? (num_entries - dentries_checked)
                                : slot_max;
            struct ktfs_dir_entry* de = (struct ktfs_dir_entry*)root_block;
            kprintf("ktfs_open: scanning dir block %d, looking for \"%s\"\n", b, name);
            // we check if the name is equal to the name we are looking for
            for (uint32_t i = 0; i < here; i++) {
                kprintf("  entry %d: name=\"%.13s\", inode=%d\n", dentries_checked + i, de[i].name, de[i].inode);
                if (ktfs_name_equals14(de[i].name, name)) {
                    uint16_t inode_num = de[i].inode;
                    cache_release_block(ktfs->cache, root_block, 0);
                    cache_release_block(ktfs->cache, inode_block, 0);
                    // we read the target inode and initialize the uio
                    uint32_t file_iblk = ibase + ((inode_num * KTFS_INOSZ) / KTFS_BLKSZ);
                    void* fblk;
                    rc = cache_get_block(ktfs->cache,
                                         (unsigned long long)file_iblk * KTFS_BLKSZ, &fblk);
                    if (rc < 0) return rc;
                    uint32_t foff = (inode_num * KTFS_INOSZ) % KTFS_BLKSZ;
                    struct ktfs_inode* fin = (struct ktfs_inode*)((char*)fblk + foff);
                    // we allocate the file structure
                    struct ktfs_file* file = kmalloc(sizeof(*file));
                    if (!file) { cache_release_block(ktfs->cache, fblk, 0); return -ENOMEM; }
                    uio_init1(&file->base, &ktfs_file_intf);
                    file->fs        = ktfs;
                    file->inode_num = inode_num;
                    file->pos       = 0;
                    file->file_size = fin->size;
                    cache_release_block(ktfs->cache, fblk, 0);
                    // we return the file pointer
                    *uioptr = &file->base;
                    return 0;
                }
            }
            // we release the root block
            cache_release_block(ktfs->cache, root_block, 0);
            dentries_checked += here;
        }
    } else if (num_entries > 0) {
        // we normal pointer-following scan for standard images
        uint32_t dentries_checked = 0;
        for (uint32_t block_idx = 0; dentries_checked < num_entries; block_idx++) {
            uint32_t dnum = 0;
            // we check if the block index is less than the number of direct data blocks
            if (block_idx < KTFS_NUM_DIRECT_DATA_BLOCKS) {
                // we get the data number from the direct block
                dnum = root_inode->block[block_idx];
                // we skip holes
                if (dnum == 0) continue;
            } else if (block_idx < KTFS_NUM_DIRECT_DATA_BLOCKS + (KTFS_BLKSZ/sizeof(uint32_t))) {
                // we check if the indirect block is allocated
                if (root_inode->indirect == 0) break;
                // we read the indirect table
                void *indblk;
                uint32_t ind_abs = ktfs_map_data_num(data_base, root_inode->indirect);
                if (ind_abs == 0) break;
                // we get the indirect block
                rc = cache_get_block(ktfs->cache, (unsigned long long)ind_abs * KTFS_BLKSZ, &indblk);
                if (rc < 0) { cache_release_block(ktfs->cache, inode_block, 0); return rc; }
                // we get the data number from the indirect table
                uint32_t *tab = (uint32_t*)indblk;
                dnum = tab[block_idx - KTFS_NUM_DIRECT_DATA_BLOCKS];
                // we release the indirect block
                cache_release_block(ktfs->cache, indblk, 0);
                if (dnum == 0) break;
            } else {
                break;
            }
            // we read the directory block
            void *dirblk;
            uint32_t abs = ktfs_map_data_num(data_base, dnum);
            if (abs == 0) break;
            // we get the directory block
            rc = cache_get_block(ktfs->cache, (unsigned long long)abs * KTFS_BLKSZ, &dirblk);
            if (rc < 0) { cache_release_block(ktfs->cache, inode_block, 0); return rc; }
            // we calculate the maximum number of slots in a block
            uint32_t slot_max = KTFS_BLKSZ / KTFS_DENSZ;
            uint32_t here = (num_entries - dentries_checked < slot_max) ? (num_entries - dentries_checked) : slot_max;
            struct ktfs_dir_entry *de = (struct ktfs_dir_entry*)dirblk;
            kprintf("ktfs_open: scanning dir block (dnum=%d), looking for \"%s\"\n", dnum, name);
            // we check if the name is equal to the name we are looking for
            for (uint32_t i = 0; i < here; i++) {
                kprintf("  entry %d: name=\"%.13s\", inode=%d\n", dentries_checked + i, de[i].name, de[i].inode);
                if (ktfs_name_equals14(de[i].name, name)) {
                    // we found the file
                    uint16_t inode_num = de[i].inode;
                    cache_release_block(ktfs->cache, dirblk, 0);
                    cache_release_block(ktfs->cache, inode_block, 0);
                    // we read the file inode
                    uint32_t fblk = ibase + ((inode_num * KTFS_INOSZ) / KTFS_BLKSZ);
                    void *f_iblock;
                    rc = cache_get_block(ktfs->cache, (unsigned long long)fblk * KTFS_BLKSZ, &f_iblock);
                    if (rc < 0) return rc;
                    uint32_t foff = (inode_num * KTFS_INOSZ) % KTFS_BLKSZ;
                    struct ktfs_inode *fin = (struct ktfs_inode*)((uint8_t*)f_iblock + foff);
                    // we allocate the file structure
                    struct ktfs_file *file = kmalloc(sizeof(*file));
                    if (!file) { 
                        cache_release_block(ktfs->cache, f_iblock, 0); return -ENOMEM; }
                    // we initialize the file structure
                    uio_init1(&file->base, &ktfs_file_intf);
                    file->fs        = ktfs;
                    file->inode_num = inode_num;
                    file->pos       = 0;
                    file->file_size = fin->size;
                    // we release the file inode block
                    cache_release_block(ktfs->cache, f_iblock, 0);
                    // we return the file pointer
                    *uioptr = &file->base;
                    return 0;
                }
            }
            // we release the directory block
            cache_release_block(ktfs->cache, dirblk, 0);
            dentries_checked += here;
        }
    }
    // we release the inode block
    cache_release_block(ktfs->cache, inode_block, 0);
    return -ENOENT;
}

/**
 * @brief Closes the file that is represented by the uio struct
 * @param uio The file io to be closed
 * @return None
 */
void ktfs_close(struct uio* uio) {
    struct ktfs_file* file = (struct ktfs_file*)uio;
    // we validate parameters
    if (!uio) {
        return;
    }
    // we free the file structure
    kfree(file);
}

/**
 * @brief Reads data from file attached to uio into provided argument buffer
 * @param uio uio of file to be read
 * @param buf Buffer to be filled
 * @param len Number of bytes to read
 * @return Number of bytes read if successful, negative error code if error
 */
long ktfs_fetch(struct uio* uio, void* buf, unsigned long len) {
    if (!uio) return -EINVAL;
    // we allow zero-byte reads even with NULL buffer
    if (len == 0) return 0;
    if (!buf) return -EINVAL;
    struct ktfs_file* file = (struct ktfs_file*)uio;
    struct ktfs_filesystem* ktfs = file->fs;
    if (!ktfs) return -EINVAL;
    // we check if we are at EOF
    if (file->pos >= file->file_size) return 0;
    // we clamp len to not exceed file size (overflow-safe)
    unsigned long long max_read = file->file_size - file->pos;
    if (len > max_read) len = max_read;
    // we fetch file inode (to get data pointers)
    uint32_t ibase = ktfs_inode_blocks_base(ktfs);
    uint32_t data_base = ktfs_data_blocks_base(ktfs);
    uint32_t iblk  = ibase + ((file->inode_num * KTFS_INOSZ) / KTFS_BLKSZ);
    void *inode_block;
    int rc = cache_get_block(ktfs->cache, (unsigned long long)iblk * KTFS_BLKSZ, &inode_block);
    if (rc < 0) return rc;
    uint32_t off = (file->inode_num * KTFS_INOSZ) % KTFS_BLKSZ;
    struct ktfs_inode *ino = (struct ktfs_inode*)((uint8_t*)inode_block + off);
    unsigned long done = 0;
    while (done < len) {
        // we calculate which block to read from
        uint32_t block_idx = (file->pos + done) / KTFS_BLKSZ;
        uint32_t in_blk_off = (file->pos + done) % KTFS_BLKSZ;
        uint32_t need = KTFS_BLKSZ - in_blk_off;
        if (need > len - done) need = len - done;
        // we get the data number for this file block
        uint32_t dnum = 0;
        if (block_idx < KTFS_NUM_DIRECT_DATA_BLOCKS) {
            // we get the data number from the direct block
            dnum = ino->block[block_idx];
            if (dnum == 0) { cache_release_block(ktfs->cache, inode_block, 0); return -EIO; }
        } else {
            uint32_t entries_per_block = KTFS_BLKSZ / sizeof(uint32_t);
            block_idx -= KTFS_NUM_DIRECT_DATA_BLOCKS;
            if (block_idx < entries_per_block) {
                // we handle single indirect blocks
                if (ino->indirect == 0) { cache_release_block(ktfs->cache, inode_block, 0); return -EIO; }
                // we read the indirect table
                void *indblk;
                uint32_t ind_abs = ktfs_map_data_num(data_base, ino->indirect);
                if (ind_abs == 0) { cache_release_block(ktfs->cache, inode_block, 0); return -EIO; }
                // we get the indirect block
                rc = cache_get_block(ktfs->cache, (unsigned long long)ind_abs * KTFS_BLKSZ, &indblk);
                if (rc < 0) { cache_release_block(ktfs->cache, inode_block, 0); return rc; }
                // we get the data number from the indirect table
                uint32_t *tab = (uint32_t*)indblk;
                dnum = tab[block_idx];
                // we release the indirect block
                cache_release_block(ktfs->cache, indblk, 0);
                if (dnum == 0) { cache_release_block(ktfs->cache, inode_block, 0); return -EIO; }
            } else {
                // we handle double indirect blocks
                block_idx -= entries_per_block;
                // we calculate which dindirect pointer and offsets within the block
                uint32_t which_dindirect = block_idx / (entries_per_block * entries_per_block);
                uint32_t remainder = block_idx % (entries_per_block * entries_per_block);
                uint32_t first_level_index = remainder / entries_per_block;
                uint32_t second_level_index = remainder % entries_per_block;
                if (which_dindirect >= KTFS_NUM_DINDIRECT_BLOCKS) {
                    cache_release_block(ktfs->cache, inode_block, 0);
                    return -EIO;
                }
                if (ino->dindirect[which_dindirect] == 0) {
                    cache_release_block(ktfs->cache, inode_block, 0);
                    return -EIO;
                }
                void *dindirect_block;
                uint32_t dind_abs = ktfs_map_data_num(data_base, ino->dindirect[which_dindirect]);
                if (dind_abs == 0) { 
                    cache_release_block(ktfs->cache, inode_block, 0); return -EIO; }
                rc = cache_get_block(ktfs->cache, (unsigned long long)dind_abs * KTFS_BLKSZ, &dindirect_block);
                if (rc < 0) { 
                    cache_release_block(ktfs->cache, inode_block, 0); return rc; }
                
                uint32_t *dindirect_tab = (uint32_t*)dindirect_block;
                uint32_t indirect_block_dnum = dindirect_tab[first_level_index];
                cache_release_block(ktfs->cache, dindirect_block, 0);
                if (indirect_block_dnum == 0) {
                    cache_release_block(ktfs->cache, inode_block, 0);
                    return -EIO;
                }
                void *indirect_block;
                uint32_t ind_abs = ktfs_map_data_num(data_base, indirect_block_dnum);
                if (ind_abs == 0) { 
                    cache_release_block(ktfs->cache, inode_block, 0); return -EIO; }
                rc = cache_get_block(ktfs->cache, (unsigned long long)ind_abs * KTFS_BLKSZ, &indirect_block);
                if (rc < 0) { 
                    cache_release_block(ktfs->cache, inode_block, 0); return rc; }
                uint32_t *indirect_tab = (uint32_t*)indirect_block;
                dnum = indirect_tab[second_level_index];
                cache_release_block(ktfs->cache, indirect_block, 0);
                if (dnum == 0) { 
                    cache_release_block(ktfs->cache, inode_block, 0); return -EIO; }
            }
        }
        // we map the data number to absolute block number
        uint32_t abs = ktfs_map_data_num(data_base, dnum);
        if (abs == 0) { 
            cache_release_block(ktfs->cache, inode_block, 0); return -EIO; }
        // we read the data block
        void *datablock;
        rc = cache_get_block(ktfs->cache, (unsigned long long)abs * KTFS_BLKSZ, &datablock);
        if (rc < 0) { 
            cache_release_block(ktfs->cache, inode_block, 0); return rc; }
        // we copy the data from the data block to the buffer
        memcpy((uint8_t*)buf + done, (uint8_t*)datablock + in_blk_off, need);
        // we release the data block
        cache_release_block(ktfs->cache, datablock, 0);
        // we update the number of bytes read
        done += need;
    }
    // we release the inode block
    cache_release_block(ktfs->cache, inode_block, 0);
    // we update the file position
    file->pos += done;
    return done;
}

/**
 * @brief Write data from the provided argument buffer into file attached to uio
 * @param uio The file to be written to
 * @param buf The buffer to be read from
 * @param len Number of bytes to write from the buffer to the file
 * @return Number of bytes written from the buffer to the file system if sucessful, negative error
 * code if error
 */
long ktfs_store(struct uio* uio, const void* buf, unsigned long len) {
    if (!uio) return -EINVAL;
    if (len == 0) return 0;
    if (!buf) return -EINVAL;
    struct ktfs_file* file = (struct ktfs_file*)uio;
    struct ktfs_filesystem* ktfs = file->fs;
    if (!ktfs) return -EINVAL;
    uint16_t ino = file->inode_num;
    uint32_t data_base = ktfs_data_blocks_base(ktfs);
    // we get the inode block
    void* inode_block;
    uint32_t inode_block_num = ktfs_inode_blocks_base(ktfs) + (ino * KTFS_INOSZ) / KTFS_BLKSZ;
    int rc = cache_get_block(ktfs->cache, (unsigned long long)inode_block_num * KTFS_BLKSZ, &inode_block);
    if (rc < 0) return rc;
    struct ktfs_inode* inode = (struct ktfs_inode*)((char*)inode_block + (ino * KTFS_INOSZ) % KTFS_BLKSZ);
    unsigned long long offset = file->pos;
    unsigned long long bytes_written = 0;
    // we write data block by block
    while (bytes_written < len) {
        // we calculate which logical block and offset within the block
        uint32_t logical_block = (offset + bytes_written) / KTFS_BLKSZ;
        uint32_t block_offset = (offset + bytes_written) % KTFS_BLKSZ;
        uint32_t to_write = KTFS_BLKSZ - block_offset;
        if (to_write > len - bytes_written) {
            to_write = len - bytes_written;
        }
        // we get or allocate the data block for this logical block
        uint32_t dnum;
        rc = ktfs_get_or_alloc_block(ktfs, inode, logical_block, &dnum, 1);
        if (rc < 0) {
            cache_release_block(ktfs->cache, inode_block, bytes_written > 0 ? 1 : 0);
            return bytes_written > 0 ? (long)bytes_written : rc;
        }
        if (dnum == 0) {
            cache_release_block(ktfs->cache, inode_block, bytes_written > 0 ? 1 : 0);
            return bytes_written > 0 ? (long)bytes_written : -ENODATABLKS;
        }
        // we get the data block
        void* data_block;
        rc = cache_get_block(ktfs->cache,(unsigned long long)ktfs_map_data_num(data_base, dnum) * KTFS_BLKSZ,&data_block);
        if (rc < 0) {
            cache_release_block(ktfs->cache, inode_block, bytes_written > 0 ? 1 : 0);
            return bytes_written > 0 ? (long)bytes_written : rc;
        }
        // we copy data from the buffer to the data block
        memcpy((char*)data_block + block_offset, (const char*)buf + bytes_written, to_write);
        cache_release_block(ktfs->cache, data_block, 1);
        bytes_written += to_write;
    }
    // we update the file size if we extended it
    if (offset + bytes_written > inode->size) {
        inode->size = offset + bytes_written;
        file->file_size = inode->size;
    }
    cache_release_block(ktfs->cache, inode_block, 1);
    file->pos += bytes_written;
    return (long)bytes_written;
}

/**
 * @brief Create a new file in the file system
 * @param fs The file system in which to create the file
 * @param name The name of the file
 * @return 0 if successful, negative error code if error
 */
int ktfs_create(struct filesystem* fs, const char* name) {
    if (!fs || !name) return -EINVAL;
    struct ktfs_filesystem* ktfs = (struct ktfs_filesystem*)fs;
    uint32_t data_base = ktfs_data_blocks_base(ktfs);
    uint16_t root_ino = ktfs->superblock.root_directory_inode;
    // we validate the file name
    if (strlen(name) == 0 || strlen(name) >= 13) {
        return -EINVAL;
    }
    // we get the root directory inode
    void* inode_block;
    uint32_t inode_block_num = ktfs_inode_blocks_base(ktfs) + (root_ino * KTFS_INOSZ) / KTFS_BLKSZ;
    int rc = cache_get_block(ktfs->cache, (unsigned long long)inode_block_num * KTFS_BLKSZ, &inode_block);
    if (rc < 0) return rc;
    struct ktfs_inode* root_inode = (struct ktfs_inode*)((char*)inode_block + (root_ino * KTFS_INOSZ) % KTFS_BLKSZ);
    uint32_t num_entries = root_inode->size / KTFS_DENSZ;
    kprintf("ktfs_create: creating \"%s\", num_entries=%d before create\n", name, num_entries);
    // we detect the "contiguous root" layout (all pointers zero but size > 0)
    int root_has_no_ptrs = (root_inode->block[0] | root_inode->block[1] | root_inode->block[2] | root_inode->block[3] | root_inode->indirect) == 0;
    // we check if the root has no pointers and the number of entries is greater than 0
    int root_contig = root_has_no_ptrs && num_entries > 0;
    kprintf("ktfs_create: root_contig=%d\n", root_contig);
    int found_duplicate = 0;
    uint32_t dentries_checked = 0;
    // we search through the root directory to check for duplicates
    if (root_contig) {
        // we fallback: scan contiguous dir blocks starting at data_base
        uint32_t need_blocks = (root_inode->size + KTFS_BLKSZ - 1) / KTFS_BLKSZ;
        for (uint32_t b = 0; b < need_blocks && dentries_checked < num_entries; b++) {
            void* dirblk;
            int rc2 = cache_get_block(ktfs->cache, (unsigned long long)(data_base + b) * KTFS_BLKSZ, &dirblk);
            if (rc2 < 0) {
                cache_release_block(ktfs->cache, inode_block, 0);
                return rc2;
            }
            // we calculate the maximum number of slots in a block
            uint32_t entries_per_block = KTFS_BLKSZ / KTFS_DENSZ;
            uint32_t here = (num_entries - dentries_checked < entries_per_block)
                                ? (num_entries - dentries_checked)
                                : entries_per_block;
            struct ktfs_dir_entry* de = (struct ktfs_dir_entry*)dirblk;
            kprintf("ktfs_create: scanning contiguous dir block %d for duplicates\n", b);
            // we check each directory entry for a duplicate name
            for (uint32_t i = 0; i < here; i++) {
                kprintf("  entry %d: name=\"%.13s\", inode=%d\n", dentries_checked + i, de[i].name, de[i].inode);
                if (ktfs_name_equals14(de[i].name, name)) {
                    found_duplicate = 1;
                    break;
                }
                dentries_checked++;
            }
            cache_release_block(ktfs->cache, dirblk, 0);
            if (found_duplicate) break;
        }
    } else {
        for (uint32_t block_idx = 0; dentries_checked < num_entries; block_idx++) {
            uint32_t dnum;
            // we get the directory block number
            if (block_idx < KTFS_NUM_DIRECT_DATA_BLOCKS) {
                dnum = root_inode->block[block_idx];
            } else {
                if (root_inode->indirect == 0) break;
                void* indblk;
                uint32_t ind_abs = ktfs_map_data_num(data_base, root_inode->indirect);
                rc = cache_get_block(ktfs->cache, (unsigned long long)ind_abs * KTFS_BLKSZ, &indblk);
                if (rc < 0) {
                    cache_release_block(ktfs->cache, inode_block, 0);
                    return rc;
                }
                uint32_t* tab = (uint32_t*)indblk;
                dnum = tab[block_idx - KTFS_NUM_DIRECT_DATA_BLOCKS];
                cache_release_block(ktfs->cache, indblk, 0);
                if (dnum == 0) break;
            }
            // we get the directory block
            void* dirblk;
            uint32_t abs = ktfs_map_data_num(data_base, dnum);
            if (abs == 0) break;
            rc = cache_get_block(ktfs->cache, (unsigned long long)abs * KTFS_BLKSZ, &dirblk);
            if (rc < 0) {
                cache_release_block(ktfs->cache, inode_block, 0);
                return rc;
            }
            uint32_t slot_max = KTFS_BLKSZ / KTFS_DENSZ;
            uint32_t here = (num_entries - dentries_checked < slot_max) ? (num_entries - dentries_checked) : slot_max;
            struct ktfs_dir_entry* de = (struct ktfs_dir_entry*)dirblk;
            kprintf("ktfs_create: scanning dir block (dnum=%d) for duplicates\n", dnum);
            // we check each directory entry for a duplicate name
            for (uint32_t i = 0; i < here; i++) {
                kprintf("  entry %d: name=\"%.13s\", inode=%d\n", dentries_checked + i, de[i].name, de[i].inode);
                if (ktfs_name_equals14(de[i].name, name)) {
                    found_duplicate = 1;
                    break;
                }
                dentries_checked++;
            }
            cache_release_block(ktfs->cache, dirblk, 0);
            if (found_duplicate) break;
        }
    }
    // we return error if file already exists
    if (found_duplicate) {
        cache_release_block(ktfs->cache, inode_block, 0);
        return -EEXIST;
    }
    // we allocate a new inode for the file
    uint16_t new_ino;
    rc = ktfs_alloc_inode(ktfs, &new_ino);
    if (rc < 0) {
        cache_release_block(ktfs->cache, inode_block, 0);
        return rc;
    }
    // we initialize the new inode
    void* new_inode_block;
    uint32_t new_inode_block_num = ktfs_inode_blocks_base(ktfs) + (new_ino * KTFS_INOSZ) / KTFS_BLKSZ;
    rc = cache_get_block(ktfs->cache, (unsigned long long)new_inode_block_num * KTFS_BLKSZ, &new_inode_block);
    if (rc < 0) {
        ktfs_free_inode(ktfs, new_ino);
        cache_release_block(ktfs->cache, inode_block, 0);
        return rc;
    }
    struct ktfs_inode* new_inode = (struct ktfs_inode*)((char*)new_inode_block + (new_ino * KTFS_INOSZ) % KTFS_BLKSZ);
    new_inode->size = 0;
    for (int i = 0; i < KTFS_NUM_DIRECT_DATA_BLOCKS; i++) {
        new_inode->block[i] = 0;
    }
    new_inode->indirect = 0;
    for (int i = 0; i < KTFS_NUM_DINDIRECT_BLOCKS; i++) {
        new_inode->dindirect[i] = 0;
    }
    cache_release_block(ktfs->cache, new_inode_block, 1);
    // we calculate where to add the new directory entry
    uint32_t entries_per_block = KTFS_BLKSZ / KTFS_DENSZ;
    uint32_t entry_block = num_entries / entries_per_block;
    uint32_t entry_offset = num_entries % entries_per_block;
    void* dirblk;
    uint32_t abs;
    if (root_contig) {
        // we use contiguous layout: directory blocks are at data_base + entry_block
        abs = data_base + entry_block;
        kprintf("ktfs_create: contiguous root, using abs=%d (data_base=%d + entry_block=%d)\n", abs, data_base, entry_block);
    } else {
        // we get or allocate the directory block for the new entry
        uint32_t dnum;
        rc = ktfs_get_or_alloc_block(ktfs, root_inode, entry_block, &dnum, 1);
        if (rc < 0 || dnum == 0) {
            ktfs_free_inode(ktfs, new_ino);
            cache_release_block(ktfs->cache, inode_block, 0);
            return rc < 0 ? rc : -ENODATABLKS;
        }
        abs = ktfs_map_data_num(data_base, dnum);
        kprintf("ktfs_create: pointer-based root, allocated dnum=%d, abs=%d\n", dnum, abs);
    }
    // we get the directory block and add the new entry
    rc = cache_get_block(ktfs->cache, (unsigned long long)abs * KTFS_BLKSZ, &dirblk);
    if (rc < 0) {
        ktfs_free_inode(ktfs, new_ino);
        cache_release_block(ktfs->cache, inode_block, 0);
        return rc;
    }
    struct ktfs_dir_entry* de = (struct ktfs_dir_entry*)dirblk;
    kprintf("ktfs_create: adding entry at block=%d, offset=%d, name=\"%s\", inode=%d\n", 
            entry_block, entry_offset, name, new_ino);
    memset(de[entry_offset].name, 0, 13);
    strncpy(de[entry_offset].name, name, 13);
    de[entry_offset].inode = new_ino;
    kprintf("ktfs_create: after write, entry name=\"%.13s\", inode=%d\n", 
            de[entry_offset].name, de[entry_offset].inode);
    cache_release_block(ktfs->cache, dirblk, 1);
    // we update the root directory size
    root_inode->size += KTFS_DENSZ;
    kprintf("ktfs_create: updated root dir size to %d (num_entries now %d)\n", 
            root_inode->size, root_inode->size / KTFS_DENSZ);
    cache_release_block(ktfs->cache, inode_block, 1);
    kprintf("ktfs_create: successfully created \"%s\"\n", name);
    return 0;
}

/**
 * @brief Deletes a certain file from the file system with the given name
 * @param fs The file system to delete the file from
 * @param name The name of the file to be deleted
 * @return 0 if successful, negative error code if error
 */
int ktfs_delete(struct filesystem* fs, const char* name) {
    if (!fs || !name) return -EINVAL;
    struct ktfs_filesystem* ktfs = (struct ktfs_filesystem*)fs;
    uint32_t data_base = ktfs_data_blocks_base(ktfs);
    uint16_t root_ino = ktfs->superblock.root_directory_inode;
    // we validate the file name
    if (strlen(name) == 0) {
        return -EINVAL;
    }
    // we get the root directory inode
    void* inode_block;
    uint32_t inode_block_num = ktfs_inode_blocks_base(ktfs) + (root_ino * KTFS_INOSZ) / KTFS_BLKSZ;
    int rc = cache_get_block(ktfs->cache, (unsigned long long)inode_block_num * KTFS_BLKSZ, &inode_block);
    if (rc < 0) return rc;
    struct ktfs_inode* root_inode = (struct ktfs_inode*)((char*)inode_block + (root_ino * KTFS_INOSZ) % KTFS_BLKSZ);
    uint32_t num_entries = root_inode->size / KTFS_DENSZ;
    // we detect the "contiguous root" layout (all pointers zero but size > 0)
    int root_has_no_ptrs = (root_inode->block[0] | root_inode->block[1] | root_inode->block[2] | root_inode->block[3] | root_inode->indirect) == 0;
    // we check if the root has no pointers and the number of entries is greater than 0
    int root_contig = root_has_no_ptrs && num_entries > 0;
    uint16_t file_ino = 0;
    uint32_t dentries_checked = 0;
    uint32_t deleted_entry_index = 0;
    int found = 0;
    // we search through the root directory to find the file
    if (root_contig) {
        // we fallback: scan contiguous dir blocks starting at data_base
        uint32_t need_blocks = (root_inode->size + KTFS_BLKSZ - 1) / KTFS_BLKSZ;
        for (uint32_t b = 0; b < need_blocks && dentries_checked < num_entries && !found; b++) {
            void* dirblk;
            rc = cache_get_block(ktfs->cache, (unsigned long long)(data_base + b) * KTFS_BLKSZ, &dirblk);
            if (rc < 0) {
                cache_release_block(ktfs->cache, inode_block, 0);
                return rc;
            }
            // we calculate the maximum number of slots in a block
            uint32_t slot_max = KTFS_BLKSZ / KTFS_DENSZ;
            uint32_t here = (num_entries - dentries_checked < slot_max)
                                ? (num_entries - dentries_checked)
                                : slot_max;
            struct ktfs_dir_entry* de = (struct ktfs_dir_entry*)dirblk;
            // we check each directory entry for a matching name
            for (uint32_t i = 0; i < here; i++) {
                if (ktfs_name_equals14(de[i].name, name)) {
                    file_ino = de[i].inode;
                    deleted_entry_index = dentries_checked + i;
                    found = 1;
                    cache_release_block(ktfs->cache, dirblk, 0);
                    break;
                }
            }
            dentries_checked += here;
            if (!found) {
                cache_release_block(ktfs->cache, dirblk, 0);
            }
        }
    } else {
        for (uint32_t block_idx = 0; dentries_checked < num_entries && !found; block_idx++) {
            uint32_t dnum;
            // we get the directory block number
            if (block_idx < KTFS_NUM_DIRECT_DATA_BLOCKS) {
                dnum = root_inode->block[block_idx];
            } else {
                if (root_inode->indirect == 0) break;
                void* indblk;
                uint32_t ind_abs = ktfs_map_data_num(data_base, root_inode->indirect);
                rc = cache_get_block(ktfs->cache, (unsigned long long)ind_abs * KTFS_BLKSZ, &indblk);
                if (rc < 0) {
                    cache_release_block(ktfs->cache, inode_block, 0);
                    return rc;
                }
                uint32_t* tab = (uint32_t*)indblk;
                dnum = tab[block_idx - KTFS_NUM_DIRECT_DATA_BLOCKS];
                cache_release_block(ktfs->cache, indblk, 0);
                if (dnum == 0) break;
            }
            // we get the directory block
            void* dirblk;
            uint32_t abs = ktfs_map_data_num(data_base, dnum);
            if (abs == 0) break;
            rc = cache_get_block(ktfs->cache, (unsigned long long)abs * KTFS_BLKSZ, &dirblk);
            if (rc < 0) {
                cache_release_block(ktfs->cache, inode_block, 0);
                return rc;
            }
            uint32_t slot_max = KTFS_BLKSZ / KTFS_DENSZ;
            uint32_t here = (num_entries - dentries_checked < slot_max) ? (num_entries - dentries_checked) : slot_max;
            struct ktfs_dir_entry* de = (struct ktfs_dir_entry*)dirblk;
            // we check each directory entry for a matching name
            for (uint32_t i = 0; i < here; i++) {
                if (ktfs_name_equals14(de[i].name, name)) {
                    file_ino = de[i].inode;
                    deleted_entry_index = dentries_checked + i;
                    found = 1;
                    cache_release_block(ktfs->cache, dirblk, 0);
                    break;
                }
            }
            dentries_checked += here;
            if (!found) {
                cache_release_block(ktfs->cache, dirblk, 0);
            }
        }
    }
    // we return error if file not found
    if (file_ino == 0) {
        cache_release_block(ktfs->cache, inode_block, 0);
        return -ENOENT;
    }
    // we get the file inode and free all its blocks
    void* file_inode_block;
    uint32_t file_inode_block_num = ktfs_inode_blocks_base(ktfs) + (file_ino * KTFS_INOSZ) / KTFS_BLKSZ;
    rc = cache_get_block(ktfs->cache, (unsigned long long)file_inode_block_num * KTFS_BLKSZ, &file_inode_block);
    if (rc < 0) {
        cache_release_block(ktfs->cache, inode_block, 0);
        return rc;
    }
    struct ktfs_inode* file_inode = (struct ktfs_inode*)((char*)file_inode_block + (file_ino * KTFS_INOSZ) % KTFS_BLKSZ);
    // we free all data blocks associated with the file
    ktfs_free_inode_blocks(ktfs, file_inode);
    cache_release_block(ktfs->cache, file_inode_block, 1);
    // we free the inode
    ktfs_free_inode(ktfs, file_ino);
    // we shift directory entries to fill the gap left by the deleted entry
    uint32_t deleted_index = deleted_entry_index;
    if (deleted_index + 1 < num_entries) {
        uint32_t entries_per_block = KTFS_BLKSZ / KTFS_DENSZ;
        for (uint32_t src_idx = deleted_index + 1; src_idx < num_entries; src_idx++) {
            uint32_t dst_idx = src_idx - 1;
            uint32_t src_block  = src_idx / entries_per_block;
            uint32_t src_offset = src_idx % entries_per_block;
            uint32_t dst_block  = dst_idx / entries_per_block;
            uint32_t dst_offset = dst_idx % entries_per_block;
            struct ktfs_dir_entry temp_entry;
            if (root_contig) {
                // we use contiguous layout: directory blocks are at data_base + block_idx
                void* src_dirblk;
                uint32_t src_abs = data_base + src_block;
                rc = cache_get_block(ktfs->cache, (unsigned long long)src_abs * KTFS_BLKSZ, &src_dirblk);
                if (rc < 0) {
                    cache_release_block(ktfs->cache, inode_block, 0);
                    return rc;
                }
                struct ktfs_dir_entry* src_de = (struct ktfs_dir_entry*)src_dirblk;
                temp_entry = src_de[src_offset];
                cache_release_block(ktfs->cache, src_dirblk, 0);
                // we write entry into destination slot
                void* dst_dirblk;
                uint32_t dst_abs = data_base + dst_block;
                rc = cache_get_block(ktfs->cache, (unsigned long long)dst_abs * KTFS_BLKSZ, &dst_dirblk);
                if (rc < 0) {
                    cache_release_block(ktfs->cache, inode_block, 0);
                    return rc;
                }
                struct ktfs_dir_entry* dst_de = (struct ktfs_dir_entry*)dst_dirblk;
                dst_de[dst_offset] = temp_entry;
                cache_release_block(ktfs->cache, dst_dirblk, 1);
            } else {
                // we get source directory block number
                uint32_t src_dnum;
                if (src_block < KTFS_NUM_DIRECT_DATA_BLOCKS) {
                    src_dnum = root_inode->block[src_block];
                } else {
                    // src is in indirect range
                    void* indblk;
                    uint32_t ind_abs = ktfs_map_data_num(data_base, root_inode->indirect);
                    if (root_inode->indirect == 0 || ind_abs == 0) {
                        cache_release_block(ktfs->cache, inode_block, 0);
                        return -EIO;
                    }
                    rc = cache_get_block(ktfs->cache, (unsigned long long)ind_abs * KTFS_BLKSZ, &indblk);
                    if (rc < 0) {
                        cache_release_block(ktfs->cache, inode_block, 0);
                        return rc;
                    }
                    uint32_t* tab = (uint32_t*)indblk;
                    src_dnum = tab[src_block - KTFS_NUM_DIRECT_DATA_BLOCKS];
                    cache_release_block(ktfs->cache, indblk, 0);
                    if (src_dnum == 0) {
                        cache_release_block(ktfs->cache, inode_block, 0);
                        return -EIO;
                    }
                }
                // we read source dir entry
                void* src_dirblk;
                uint32_t src_abs = ktfs_map_data_num(data_base, src_dnum);
                rc = cache_get_block(ktfs->cache, (unsigned long long)src_abs * KTFS_BLKSZ, &src_dirblk);
                if (rc < 0) {
                    cache_release_block(ktfs->cache, inode_block, 0);
                    return rc;
                }
                struct ktfs_dir_entry* src_de = (struct ktfs_dir_entry*)src_dirblk;
                temp_entry = src_de[src_offset];
                cache_release_block(ktfs->cache, src_dirblk, 0);
                // we get destination directory block number
                uint32_t dst_dnum;
                if (dst_block < KTFS_NUM_DIRECT_DATA_BLOCKS) {
                    dst_dnum = root_inode->block[dst_block];
                } else {
                    void* indblk;
                    uint32_t ind_abs = ktfs_map_data_num(data_base, root_inode->indirect);
                    if (root_inode->indirect == 0 || ind_abs == 0) {
                        cache_release_block(ktfs->cache, inode_block, 0);
                        return -EIO;
                    }
                    rc = cache_get_block(ktfs->cache, (unsigned long long)ind_abs * KTFS_BLKSZ, &indblk);
                    if (rc < 0) {
                        cache_release_block(ktfs->cache, inode_block, 0);
                        return rc;
                    }
                    uint32_t* tab = (uint32_t*)indblk;
                    dst_dnum = tab[dst_block - KTFS_NUM_DIRECT_DATA_BLOCKS];
                    cache_release_block(ktfs->cache, indblk, 0);
                    if (dst_dnum == 0) {
                        cache_release_block(ktfs->cache, inode_block, 0);
                        return -EIO;
                    }
                }
                // we write entry into destination slot
                void* dst_dirblk;
                uint32_t dst_abs = ktfs_map_data_num(data_base, dst_dnum);
                rc = cache_get_block(ktfs->cache, (unsigned long long)dst_abs * KTFS_BLKSZ, &dst_dirblk);
                if (rc < 0) {
                    cache_release_block(ktfs->cache, inode_block, 0);
                    return rc;
                }
                struct ktfs_dir_entry* dst_de = (struct ktfs_dir_entry*)dst_dirblk;
                dst_de[dst_offset] = temp_entry;
                cache_release_block(ktfs->cache, dst_dirblk, 1);
            }
        }
    }
    // we update the root directory size
    root_inode->size -= KTFS_DENSZ;
    cache_release_block(ktfs->cache, inode_block, 1);
    return 0;
}

/**
 * @brief Given a file io object, a specific command, and possibly some arguments, execute the
 * corresponding functions
 * @details Any commands such as (FCNTL_GETEND, FCNTL_GETPOS, ...) should pass back through the arg
 * variable. Do not directly return the value.
 * @details FCNTL_GETEND should pass back the size of the file in bytes through the arg variable.
 * @details FCNTL_SETEND should set the size of the file to the value passed in through arg.
 * @details FCNTL_GETPOS should pass back the current position of the file pointer in bytes through
 * the arg variable.
 * @details FCNTL_SETPOS should set the current position of the file pointer to the value passed in
 * through arg.
 * @param uio the uio object of the file to perform the control function
 * @param cmd the operation to execute. KTFS should support FCNTL_GETEND, FCNTL_SETEND (CP2),
 * FCNTL_GETPOS, FCNTL_SETPOS.
 * @param arg the argument to pass in, may be different for different control functions
 * @return 0 if successful, negative error code if error
 */
int ktfs_cntl(struct uio* uio, int cmd, void* arg) {
    struct ktfs_file* file = (struct ktfs_file*)uio;
    struct ktfs_filesystem* ktfs;
    if (!file) {
        return -EINVAL;
    }
    ktfs = file->fs;
    if (!ktfs) {
        return -EINVAL;
    }
    switch (cmd) {
        case FCNTL_GETEND:
            // we now return the file size
            if (!arg) {
                return -EINVAL;
            }
            *(unsigned long long*)arg = file->file_size;
            return 0;
        case FCNTL_GETPOS:
            // we now return the current position
            if (!arg) {
                return -EINVAL;
            }
            *(unsigned long long*)arg = file->pos;
            return 0;
        case FCNTL_SETPOS: {
            if (!arg) {
                return -EINVAL;
            }
            unsigned long long new_pos = *(unsigned long long*)arg;
            // Validate position doesn't exceed file size
            if (new_pos > file->file_size) {
                return -EINVAL;
            }
            file->pos = new_pos;
            return 0;
        }
        case FCNTL_SETEND: {
            if (!arg) {
                return -EINVAL;
            }
            unsigned long long new_size = *(unsigned long long*)arg;
            uint16_t ino = file->inode_num;
            uint32_t data_base = ktfs_data_blocks_base(ktfs);
            void* inode_block;
            uint32_t inode_block_num = ktfs_inode_blocks_base(ktfs) + (ino * KTFS_INOSZ) / KTFS_BLKSZ;
            int rc = cache_get_block(ktfs->cache, (unsigned long long)inode_block_num * KTFS_BLKSZ, &inode_block);
            if (rc < 0) return rc;
            struct ktfs_inode* inode = (struct ktfs_inode*)((char*)inode_block + (ino * KTFS_INOSZ) % KTFS_BLKSZ);
            if (new_size < inode->size) {
                cache_release_block(ktfs->cache, inode_block, 0);
                return -ENOTSUP;
            }
            if (new_size == inode->size) {
                cache_release_block(ktfs->cache, inode_block, 0);
                return 0;
            }
            uint32_t old_blocks = (inode->size + KTFS_BLKSZ - 1) / KTFS_BLKSZ;
            uint32_t new_blocks = (new_size + KTFS_BLKSZ - 1) / KTFS_BLKSZ;
            for (uint32_t i = old_blocks; i < new_blocks; i++) {
                uint32_t dnum;
                rc = ktfs_get_or_alloc_block(ktfs, inode, i, &dnum, 1);
                if (rc < 0 || dnum == 0) {
                    cache_release_block(ktfs->cache, inode_block, 1);
                    return rc < 0 ? rc : -ENODATABLKS;
                }
                void* data_block;
                rc = cache_get_block(ktfs->cache,(unsigned long long)ktfs_map_data_num(data_base, dnum) * KTFS_BLKSZ,&data_block);
                if (rc < 0) {
                    cache_release_block(ktfs->cache, inode_block, 1);
                    return rc;
                }
                memset(data_block, 0, KTFS_BLKSZ);
                cache_release_block(ktfs->cache, data_block, 1);
            }
            if (inode->size % KTFS_BLKSZ != 0 && old_blocks > 0) {
                uint32_t dnum;
                rc = ktfs_get_or_alloc_block(ktfs, inode, old_blocks - 1, &dnum, 0);
                if (rc == 0 && dnum != 0) {
                    void* data_block;
                    rc = cache_get_block(ktfs->cache,(unsigned long long)ktfs_map_data_num(data_base, dnum) * KTFS_BLKSZ,&data_block);
                    if (rc == 0) {
                        uint32_t old_offset = inode->size % KTFS_BLKSZ;
                        uint32_t zero_len = KTFS_BLKSZ - old_offset;
                        if (new_size < (old_blocks * KTFS_BLKSZ)) {
                            zero_len = new_size - inode->size;
                        }
                        memset((char*)data_block + old_offset, 0, zero_len);
                        cache_release_block(ktfs->cache, data_block, 1);
                    }
                }
            }
            inode->size = new_size;
            file->file_size = new_size;
            cache_release_block(ktfs->cache, inode_block, 1);
            return 0;
        }
        default:
            return -ENOTSUP;
    }
}

/**
 * @brief Flushes the cache to the backing device
 * @return 0 if flush successful, negative error code if error
 */
void ktfs_flush(struct filesystem* fs) {
    struct ktfs_filesystem* ktfs = (struct ktfs_filesystem*)fs;
    int result;
    // we validate parameters
    if (!fs) {
        return;
    }
    // we now flush the cache to the backing device
    result = cache_flush(ktfs->cache);
    // we now check if an error occurs
    if (result < 0) {
        return;
    }
}

/**
 * @brief Closes the listing device represented by the uio pointer
 * @param uio The uio pointer of ls
 * @return None
 */
void ktfs_listing_close(struct uio* uio) {
    struct ktfs_listing_uio* ls = (struct ktfs_listing_uio*)uio;
    if (ls) {
        kfree(ls);
    }
}

/**
 * @brief Reads all of the files names in the file system using ls and copies them into the
 * providied buffer
 * @param uio The uio pointer of ls
 * @param buf The buffer to copy the file names to
 * @param bufsz The size of the buffer
 * @return The size written to the buffer
 */
long ktfs_listing_read(struct uio* uio, void* buf, unsigned long bufsz) {
    struct ktfs_listing_uio* ls = (struct ktfs_listing_uio*)uio;
    struct ktfs_filesystem* ktfs;
    uint16_t root_ino;
    uint32_t ibase, data_base, root_blk;
    void* inode_block;
    struct ktfs_inode* root_inode;
    int rc;
    if (!ls || !buf || bufsz == 0) return -EINVAL;
    //we need to get the file system
    ktfs = ls->fs;
    //we need to check if we've read all entries
    if (ls->current_entry >= ls->total_entries) {
        return 0;  // EOF - no more files
    }
    //we need to get the root directory inode
    root_ino = ktfs->superblock.root_directory_inode;
    ibase = ktfs_inode_blocks_base(ktfs);
    data_base = ktfs_data_blocks_base(ktfs);
    root_blk = ibase + ((root_ino * KTFS_INOSZ) / KTFS_BLKSZ);
    //we need to get the inode block
    rc = cache_get_block(ktfs->cache, (unsigned long long)root_blk * KTFS_BLKSZ, &inode_block);
    if (rc < 0) return rc;
    //we need to get the offset
    uint32_t off = (root_ino * KTFS_INOSZ) % KTFS_BLKSZ;
    root_inode = (struct ktfs_inode*)((uint8_t*)inode_block + off);
    //we need to calculate which block and offset within block for current entry
    uint32_t entries_per_block = KTFS_BLKSZ / KTFS_DENSZ;
    uint32_t block_idx = ls->current_entry / entries_per_block;
    uint32_t entry_offset = ls->current_entry % entries_per_block;
    //we need to get the data block number for this directory block
    uint32_t dnum = 0;
    //we need to check if root has no pointers (contiguous layout)
    int root_has_no_ptrs = (root_inode->block[0] | root_inode->block[1] | root_inode->block[2] | root_inode->block[3] | root_inode->indirect) == 0;
    if (root_has_no_ptrs && ls->total_entries > 0) {
        //we need to handle contiguous layout: directory blocks start at data_base
        dnum = block_idx;  
    } 
    else if (block_idx < KTFS_NUM_DIRECT_DATA_BLOCKS) {
        //we need to get the data block number from the direct block
        dnum = root_inode->block[block_idx];
    } 
    else if (root_inode->indirect != 0) {
        //we need to handle indirect block
        uint32_t ind_offset = block_idx - KTFS_NUM_DIRECT_DATA_BLOCKS;
        void* indblk;
        uint32_t ind_abs = ktfs_map_data_num(data_base, root_inode->indirect);
        rc = cache_get_block(ktfs->cache, (unsigned long long)ind_abs * KTFS_BLKSZ, &indblk);
        if (rc < 0) {
            cache_release_block(ktfs->cache, inode_block, 0);
            return rc;
        }
        //we need to get the data block number from the indirect block
        uint32_t* tab = (uint32_t*)indblk;
        dnum = tab[ind_offset];
        cache_release_block(ktfs->cache, indblk, 0);
    }
    if (dnum == 0 && !root_has_no_ptrs) {
        cache_release_block(ktfs->cache, inode_block, 0);
        return -EIO;
    }
    //we need to read the directory block
    void* dirblk;
    uint32_t abs = root_has_no_ptrs ? (data_base + dnum) : ktfs_map_data_num(data_base, dnum);
    rc = cache_get_block(ktfs->cache, (unsigned long long)abs * KTFS_BLKSZ, &dirblk);
    if (rc < 0) {
        cache_release_block(ktfs->cache, inode_block, 0);
        return rc;
    }
    //we need to get the directory entry
    struct ktfs_dir_entry* de = (struct ktfs_dir_entry*)dirblk;
    struct ktfs_dir_entry* current_de = &de[entry_offset];
    //we need to extract filename (null-terminated, max 13 chars)
    char filename[14];
    memcpy(filename, current_de->name, 13);
    filename[13] = '\0';
    //we need to copy filename to buffer
    size_t len = strlen(filename);
    if (len >= bufsz) {
        len = bufsz - 1;
        memcpy(buf, filename, len);
        ((char*)buf)[len] = '\0';
    } else {
        memcpy(buf, filename, len);
        ((char*)buf)[len] = '\0';
    }
    //we need to release the blocks
    cache_release_block(ktfs->cache, dirblk, 0);
    cache_release_block(ktfs->cache, inode_block, 0);
    //we need to move to next entry
    ls->current_entry++;
    return len;
}
