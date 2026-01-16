
/*! @file ramdisk.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Memory-backed storage implementation
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifdef RAMDISK_DEBUG
#define DEBUG
#endif

#ifdef RAMDISK_TRACE
#define TRACE
#endif

#include <stddef.h>

#include "console.h"
#include "devimpl.h"
#include "error.h"
#include "heap.h"
#include "misc.h"
#include "string.h"
#include "uio.h"

#ifndef RAMDISK_NAME
#define RAMDISK_NAME "ramdisk"
#endif

// INTERNAL TYPE DEFINITIONS
//

/**
 * @brief Storage device backed by a block of memory. Allows modification of the backing memory
 * block.
 */
struct ramdisk {
    struct storage storage;  ///< Storage struct of memory storage
    void *buf;               ///< Block of memory
    size_t size;             ///< Size of memory block
};

// INTERNAL FUNCTION DECLARATIONS
//

static int ramdisk_open(struct storage *sto);
static void ramdisk_close(struct storage *sto);
static long ramdisk_fetch(struct storage *sto, unsigned long long pos, void *buf,
                          unsigned long bytecnt);
static int ramdisk_cntl(struct storage *sto, int cmd, void *arg);

// INTERNAL GLOBAL CONSTANTS
//
static const struct storage_intf ramdisk_intf = {
    .blksz = 1,
    .open = &ramdisk_open,
    .close = &ramdisk_close,
    .fetch = &ramdisk_fetch,
    .store = NULL,  // Read-only storage (blob data in .rodata)
    .cntl = &ramdisk_cntl};

// EXPORTED FUNCTION DEFINITIONS
//
/**
 * @brief Creates and registers a memory-backed storage device
 * @return None
 */
void ramdisk_attach() {
    // External symbols from linker script for embedded blob data
    extern char _kimg_blob_start[], _kimg_blob_end[];
    // we now allocate the ramdisk structure
    struct ramdisk *rd = kmalloc(sizeof(struct ramdisk));
    if (!rd) {
        kprintf("ramdisk_attach: failed to allocate ramdisk structure\n");
        return;
    }
    // we now calculate the size of the blob
    rd->buf = _kimg_blob_start;
    rd->size = _kimg_blob_end - _kimg_blob_start;
    // we now initialize the storage interface
    storage_init(&rd->storage, &ramdisk_intf, rd->size);
    // we now register the device
    int result = register_device(RAMDISK_NAME, DEV_STORAGE, &rd->storage);
    if (result < 0) {
        kprintf("ramdisk_attach: failed to register device\n");
        kfree(rd);
    }
}
// INTERNAL FUNCTION DEFINITIONS
//
/**
 * @brief Opens the _ramdisk_ device.
 * @param sto Storage struct pointer for memory storage
 * @return 0 on success, negative error code on failure
 */
static int ramdisk_open(struct storage *sto) {
    // we now validate the parameter
    if (!sto) {
        return -EINVAL;
    }
    // we now return that the ramdisk is always ready
    return 0;
}
/**
 * @brief Closes the _ramdisk_ device.
 * @param sto Storage struct pointer for memory storage
 */
static void ramdisk_close(struct storage *sto) {
    // we now validate the parameter
    if (!sto) {
        return;
    }
    // we now return that the ramdisk doesn't need cleanup
    return;
}
/**
 * @brief Reads bytecnt number of bytes from the disk and writes them to buf.
 * @details Performs proper bounds checks, then copies data from memory block to passed buffer
 * @param sto Storage struct pointer for memory storage
 * @param pos Position in storage to read from
 * @param buf Buffer to copy data from memory to
 * @param bytecnt Number of bytes to read from memory
 * @return Number of bytes successfully read
 */
static long ramdisk_fetch(struct storage *sto, unsigned long long pos, void *buf,
                          unsigned long bytecnt) {
    // we now validate the storage parameter
    if (!sto) {
        return -EINVAL;
    }
    // we now handle zero-length reads (buf may be NULL for 0-byte reads)
    if (bytecnt == 0) {
        return 0;
    }
    // we now validate the buffer parameter (required for non-zero reads)
    if (!buf) {
        return -EINVAL;
    }
    // we now get the ramdisk struct from the storage struct
    struct ramdisk *rd = (struct ramdisk *)((char *)sto - offsetof(struct ramdisk, storage));
    // we now check the bounds - position must be within storage
    if (pos >= rd->size) {
        return 0;  // EOF
    }
    // we now clamp bytecnt to not exceed available data (overflow-safe)
    unsigned long max = rd->size - pos;
    if (bytecnt > max) {
        bytecnt = max;
    }
    // we now copy data from ramdisk to buffer
    memcpy(buf, (char *)rd->buf + pos, bytecnt);
    return bytecnt;
}
/**
 * @brief _cntl_ functions for memory storage.
 * @details Memory storage supports basic control operations
 * @details Any commands such as FCNTL_GETEND should pass back through the arg variable. Do not
 * directly return the value.
 * @details FCNTL_GETEND should return the capacity of the VirtIO block device in bytes.
 * @param sto Storage struct pointer for memory storage
 * @param cmd command to execute. ramdisk should support FCNTL_GETEND.
 * @param arg Argument for commands
 * @return 0 on success, error on failure or unsupported command
 */
static int ramdisk_cntl(struct storage *sto, int cmd, void *arg) {
    // we now validate the storage parameter
    if (!sto) {
        return -EINVAL;
    }
    // we now handle FCNTL_GETEND - return capacity in bytes
    if (cmd == FCNTL_GETEND) {
        if (!arg) {
            return -EINVAL;
        }
        *(unsigned long long *)arg = sto->capacity;
        return 0;
    }
    // we now return that all other commands are unsupported
    return -ENOTSUP;
}
