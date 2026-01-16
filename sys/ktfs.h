/*! @file ktfs.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief KTFS Header File.
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#include <stdint.h>

#include "uioimpl.h"

#define KTFS_BLKSZ 512
#define KTFS_INOSZ 32
#define KTFS_DENSZ 16
#define KTFS_MAX_FILENAME_LEN KTFS_DENSZ - sizeof(uint16_t) - sizeof(uint8_t)
#define KTFS_NUM_DIRECT_DATA_BLOCKS 4
#define KTFS_NUM_INDIRECT_BLOCKS 1
#define KTFS_NUM_DINDIRECT_BLOCKS 2
#define KTFS_MAX_FILE_SIZE                                                     \
    (KTFS_NUM_DIRECT_DATA_BLOCKS * KTFS_BLKSZ +                                \
     KTFS_NUM_INDIRECT_BLOCKS * (KTFS_BLKSZ / sizeof(uint32_t)) * KTFS_BLKSZ + \
     KTFS_NUM_DINDIRECT_BLOCKS * (KTFS_BLKSZ / sizeof(uint32_t)) *             \
         (KTFS_BLKSZ / sizeof(uint32_t)) * KTFS_BLKSZ)

#define KTFS_FILE_IN_USE (1 << 0)
#define KTFS_FILE_FREE (0 << 0)
#define KTFS_NO_ALLOCATION 0
#define KTFS_ALLOCATE 1

/*
Overall filesystem image layout

+------------------+
|   Superblock     |
+------------------+
|     Padding      |
+------------------+
|  Inode Bitmap 0  |
+------------------+
|  Inode Bitmap 1  |
+------------------+
|      ...         |
+------------------+
|   Bitmap Blk 0   |
+------------------+
|   Bitmap Blk 1   |
+------------------+
|      ...         |
+------------------+
|   Inode Blk 0    |
+------------------+
|   Inode Blk 1    |
+------------------+
|      ...         |
+------------------+
|    Data Blk 0    |
+------------------+
|    Data Blk 1    |
+------------------+
|      ...         |
+------------------+

Imagining this layout as a struct, it would look like this:

struct filesystem {
    struct ktfs_superblock superblock;
    uint8_t padding[BLOCK_SIZE - sizeof(ktfs_superblock)];
    struct ktfs_bitmap inode_bitmaps[];
    struct ktfs_bitmap bitmaps[];
    struct ktfs_inode inodes[];
    struct ktfs_data_block data_blocks[];
};

NOTE: The ((packed)) attribute is used to ensure that the struct is packed and
there is no padding between the members or struct alignment requirements.
*/

// Superblock
/// @brief Struct representing KTFS superblock make-up
struct ktfs_superblock {
    /// Number of blocks
    uint32_t block_count;

    /// Number of blocks allocated for inode bitmap
    uint32_t inode_bitmap_block_count;

    /// Number of blocks allocated for available block bitmap
    uint32_t bitmap_block_count;

    /// Number of blocks allocated for inodes
    uint32_t inode_block_count;

    /// The root directory inode number
    uint16_t root_directory_inode;
} __attribute__((packed));

// Inode with indirect and doubly-indirect blocks
/// @brief Struct representing a KTFS inode
struct ktfs_inode {
    uint32_t size;                                  // Size in bytes
    uint32_t block[KTFS_NUM_DIRECT_DATA_BLOCKS];    // Direct block indices
    uint32_t indirect;                              // Indirect block index
    uint32_t dindirect[KTFS_NUM_DINDIRECT_BLOCKS];  // Doubly-indirect block indices
} __attribute__((packed));

// Directory entry
/// @brief Struct representing file entry in directory
struct ktfs_dir_entry {
    /// Inode number
    uint16_t inode;
    /// File name (plus null terminator)
    char name[KTFS_MAX_FILENAME_LEN + sizeof(uint8_t)];
} __attribute__((packed));

// Bitmap block
/// @brief Struct representing available block/inode bitmap
struct ktfs_bitmap {
    uint8_t bytes[KTFS_BLKSZ];
} __attribute__((packed));

/// @brief Struct representing raw data block
struct ktfs_data_block {
    uint8_t data[KTFS_BLKSZ];
} __attribute__((packed));
