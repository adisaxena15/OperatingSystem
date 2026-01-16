


/*! @file vioblk.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief VirtIO block device
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#include "devimpl.h"
#ifdef VIOBLK_TRACE
#define TRACE
#endif

#ifdef VIOBLK_DEBUG
#define DEBUG
#endif

#include <limits.h>

#include "conf.h"
#include "console.h"
#include "device.h"
#include "error.h"
#include "heap.h"
#include "intr.h"
#include "misc.h"
#include "string.h"
#include "thread.h"
#include "uio.h"  // FCNTL
#include "virtio.h"

// COMPILE-TIME PARAMETERS
//

#ifndef VIOBLK_INTR_PRIO
#define VIOBLK_INTR_PRIO 1
#endif

#ifndef VIOBLK_NAME
#define VIOBLK_NAME "vioblk"
#endif

// INTERNAL CONSTANT DEFINITIONS
//

// VirtIO block device feature bits (number, *not* mask)

#define VIRTIO_BLK_F_SIZE_MAX 1
#define VIRTIO_BLK_F_SEG_MAX 2
#define VIRTIO_BLK_F_GEOMETRY 4
#define VIRTIO_BLK_F_RO 5
#define VIRTIO_BLK_F_BLK_SIZE 6
#define VIRTIO_BLK_F_FLUSH 9
#define VIRTIO_BLK_F_TOPOLOGY 10
#define VIRTIO_BLK_F_CONFIG_WCE 11
#define VIRTIO_BLK_F_MQ 12
#define VIRTIO_BLK_F_DISCARD 13
#define VIRTIO_BLK_F_WRITE_ZEROES 14
#define VIRTIO_BLK_T_IN 0   
#define VIRTIO_BLK_T_OUT 1  
#define VIRTIO_BLK_S_OK 0
#define VIRTIO_BLK_S_IOERR 1
#define VIRTIO_BLK_S_UNSUPP 2
#ifndef VIOBLK_QUEUE_SIZE
#define VIOBLK_QUEUE_SIZE 8
#endif

// INTERNAL TYPE DEFINITIONS
//
struct virtio_blk_req_header {
    uint32_t type;      
    uint32_t reserved;  
    uint64_t sector;   
};
struct vioblk_storage {
    struct storage base;                            
    volatile struct virtio_mmio_regs* regs;         
    int irqno;                                      
    unsigned int blksz;                             
    char opened;                                    
    struct virtq_desc desc_table[VIOBLK_QUEUE_SIZE * 3];  
    struct virtq_avail* avail_ring;                 
    struct virtq_used* used_ring;                   
    uint16_t last_used_index;                       
    struct condition request_done;                  
    struct virtio_blk_req_header req_header;        
    char data_buffer[512];                          
    uint8_t status_byte;                            
    struct lock vioblk_lock;                       
};

// INTERNAL FUNCTION DECLARATIONS
//

/**
 * @brief Sets the virtq avail and virtq used queues such that they are available for use. (Hint,
 * read virtio.h) Enables the interupt line for the virtio device and sets necessary flags in vioblk
 * device.
 * @param sto Storage IO struct for the storage device
 * @return Return 0 on success or negative error code if error. If the given sto is already opened,
 * then return -EBUSY.
 */
static int vioblk_storage_open(struct storage* sto);

/**
 * @brief Resets the virtq avail and virtq used queues and sets necessary flags in vioblk device. If
 * the given sto is not opened, this function does nothing.
 * @param sto Storage IO struct for the storage device
 * @return None
 */
static void vioblk_storage_close(struct storage* sto);

/**
 * @brief Reads bytecnt number of bytes from the disk and writes them to buf. Achieves this by
 * repeatedly setting the appropriate registers to request a block from the disk, waiting until the
 * data has been populated in block buffer cache, and then writes that data out to buf. Thread
 * sleeps while waiting for the disk to service the request.
 * @param sto Storage IO struct for the storage device
 * @param pos The starting position for the read within the VirtIO device
 * @param buf A pointer to the buffer to fill with the read data
 * @param bytecnt The number of bytes to read from the VirtIO device into the buffer
 * @return The number of bytes read from the device, or negative error code if error
 */
static long vioblk_storage_fetch(struct storage* sto, unsigned long long pos, void* buf,
                                 unsigned long bytecnt);

/**
 * @brief Writes bytecnt number of bytes from the parameter buf to the disk. The size of the virtio
 * device should not change. You should only overwrite existing data. Write should also not create
 * any new files. Achieves this by filling up the block buffer cache and then setting the
 * appropriate registers to request the disk write the contents of the cache to the specified block
 * location. Thread sleeps while waiting for the disk to service the request.
 * @param sto Storage IO struct for the storage device
 * @param pos The starting position for the write within the VirtIO device
 * @param buf A pointer to the buffer with the data to write
 * @param bytecnt The number of bytes to write to the VirtIO device from the buffer
 * @return The number of bytes written to the device, or negative error code if error
 */
static long vioblk_storage_store(struct storage* sto, unsigned long long pos, const void* buf,
                                 unsigned long bytecnt);

/**
 * @brief Given a file io object, a specific command, and possibly some arguments, execute the
 * corresponding functions on the VirtIO block device.
 * @details Any commands such as FCNTL_GETEND should pass back through the arg variable. Do not
 * directly return the value.
 * @details FCNTL_GETEND should return the capacity of the VirtIO block device in bytes.
 * @param sto Storage IO struct for the storage device
 * @param op Operation to execute. vioblk should support FCNTL_GETEND.
 * @param arg Argument specific to the operation being performed
 * @return Status code on the operation performed
 */
static int vioblk_storage_cntl(struct storage* sto, int op, void* arg);

/**
 * @brief The interrupt handler for the VirtIO device. When an interrupt occurs, the system will
 * call this function.
 * @param irqno The interrupt request number for the VirtIO device
 * @param aux A generic pointer for auxiliary data.
 * @return None
 */
static void vioblk_isr(int irqno, void* aux);
static const struct storage_intf vioblk_storage_intf = {
    .blksz = 512,  
    .open = &vioblk_storage_open,
    .close = &vioblk_storage_close,
    .fetch = &vioblk_storage_fetch,
    .store = &vioblk_storage_store,
    .cntl = &vioblk_storage_cntl};

// EXPORTED FUNCTION DEFINITIONS
//

// Attaches a VirtIO block device. Declared and called directly from virtio.c.
/**
 * @brief Initializes virtio block device with the necessary IO operation functions and sets the
 * required feature bits.
 * @param regs Memory mapped register of Virtio
 * @param irqno Interrupt request number of the device
 * @return None
 */

// void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno)
// Inputs: volatile struct virtio_mmio_regs * regs - Pointer to VirtIO MMIO registers
//         int irqno - Interrupt number for the device
// Outputs: None (void)
// Description: Attaches and initializes VirtIO block device. Negotiates features, allocates device structure, sets up virtqueue with descriptor/avail/used rings, and registers device.
// Side Effects: Allocates memory for device structure and virtqueues, initializes descriptor table, attaches virtqueue, sets device status to DRIVER_OK, registers device with device manager
void vioblk_attach(volatile struct virtio_mmio_regs* regs, int irqno) {
    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct vioblk_storage* vbd;
    unsigned int blksz;
    int result;

    trace("%s(regs=%p,irqno=%d)", __func__, regs, irqno);

    assert(regs->device_id == VIRTIO_ID_BLOCK);

    // Signal device that we found a driver

    regs->status |= VIRTIO_STAT_DRIVER;
    __sync_synchronize();  // fence o,io

    // Negotiate features. We need:
    //  - VIRTIO_F_RING_RESET and
    //  - VIRTIO_F_INDIRECT_DESC
    // We want:
    //  - VIRTIO_BLK_F_BLK_SIZE and
    //  - VIRTIO_BLK_F_TOPOLOGY.

    virtio_featset_init(needed_features);
    virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
    virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
    virtio_featset_init(wanted_features);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_TOPOLOGY);
    result = virtio_negotiate_features(regs, enabled_features, wanted_features, needed_features);

    if (result != 0) {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    // If the device provides a block size, use it. Otherwise, use 512.

    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE))
        blksz = regs->config.blk.blk_size;
    else
        blksz = 512;

    // blksz must be a power of two
    assert(((blksz - 1) & blksz) == 0);

    // we allocate and initialize device struct
    vbd = kcalloc(1, sizeof(struct vioblk_storage));
    if (vbd == NULL) {
        kprintf("%p: failed to allocate vioblk device structure\n", regs);
        return;
    }
    vbd->regs = regs;
    vbd->irqno = irqno;
    vbd->blksz = blksz;
    // we set opened to 0 as the device is not opened
    vbd->opened = 0;
    // we set last_used_index to 0 as the device has not seen any used index
    vbd->last_used_index = 0;
    // we initialize condition variable
    condition_init(&vbd->request_done, "vioblk.request_done");
    // we initialize lock for concurrent access protection
    lock_init(&vbd->vioblk_lock);
    // we allocate virtq_avail and virtq_used structures
    vbd->avail_ring = kcalloc(1, VIRTQ_AVAIL_SIZE(VIOBLK_QUEUE_SIZE));
    vbd->used_ring = kcalloc(1, VIRTQ_USED_SIZE(VIOBLK_QUEUE_SIZE));
    if (vbd->avail_ring == NULL || vbd->used_ring == NULL) {
        kprintf("%p: failed to allocate virtqueues\n", regs);
        return;
    }
    // we attach the virtqueue (queue 0 is the request queue for block devices)
    virtio_attach_virtq(regs, 0, VIOBLK_QUEUE_SIZE, (uint64_t)vbd->desc_table,
                        (uint64_t)vbd->used_ring, (uint64_t)vbd->avail_ring);
    regs->status |= VIRTIO_STAT_DRIVER_OK; 
    // fence o,oi
    __sync_synchronize();
    // we get capacity from device config
    uint64_t capacity_sectors = regs->config.blk.capacity;
    uint64_t capacity_bytes = capacity_sectors * 512;
    // we initialize storage interface and register device
    storage_init(&vbd->base, &vioblk_storage_intf, capacity_bytes);
    register_device(VIOBLK_NAME, DEV_STORAGE, vbd);
}
// int vioblk_storage_open(struct storage * sto)
// Inputs: struct storage * sto - Pointer to the storage device structure
// Outputs: int - 0 on success, -EBUSY if already opened
// Description: Opens VirtIO block device, enables virtqueue, and registers interrupt handler
// Side Effects: Enables virtqueue 0, registers ISR, marks device as opened
int vioblk_storage_open(struct storage* sto) {
    struct vioblk_storage* const vbd =
        (void*)sto - offsetof(struct vioblk_storage, base);
    // we check if already opened
    if (vbd->opened)
        return -EBUSY;
    // we enable the virtqueue (queue 0)
    virtio_enable_virtq(vbd->regs, 0);
    // we enable interrupt source with ISR
    enable_intr_source(vbd->irqno, VIOBLK_INTR_PRIO, vioblk_isr, vbd);
    // we mark as opened
    vbd->opened = 1;
    return 0;
}
// void vioblk_storage_close(struct storage * sto)
// Inputs: struct storage * sto - Pointer to the storage device structure
// Outputs: None (void)
// Description: Closes VirtIO block device by resetting virtqueue and disabling interrupts
// Side Effects: Resets virtqueue 0, disables interrupt source, marks device as closed
void vioblk_storage_close(struct storage* sto) {
    struct vioblk_storage* const vbd =
        (void*)sto - offsetof(struct vioblk_storage, base);
    // we only close if currently opened
    if (!vbd->opened)
        return;
    // we reset the virtqueue (queue 0)
    virtio_reset_virtq(vbd->regs, 0);
    // we disable interrupt source
    disable_intr_source(vbd->irqno);
    // we mark as closed
    vbd->opened = 0;
}
// long vioblk_storage_fetch(struct storage * sto, unsigned long long pos, void * buf, unsigned long bytecnt)
// Inputs: struct storage * sto - Pointer to the storage device structure
//         unsigned long long pos - Byte offset into the block device
//         void * buf - Buffer to read into
//         unsigned long bytecnt - Number of bytes to fetch
// Outputs: long - Number of bytes read from device, or negative error code on failure
// Description: Reads bytecnt number of bytes from the disk and writes them to buf, rounded down to nearest blksz. Achieves this by repeatedly setting the appropriate registers to request a block from the disk, waiting until the data has been populated in block buffer, and then writes that data out to buf. The request is truncated if attempted to read past the end. Thread sleeps while waiting for the disk to service the request.
// Side Effects: Updates descriptor and available ring, notifies device, waits on condition variable, copies data to buffer
long vioblk_storage_fetch(struct storage* sto, unsigned long long pos, void* buf,
                                 unsigned long bytecnt) {
    // we validate storage parameter
    if (!sto)
        return -EINVAL;
    struct vioblk_storage* const vbd =
        (void*)sto - offsetof(struct vioblk_storage, base);
    unsigned long bytes_read = 0;
    // we check if device is opened
    if (!vbd->opened)
        return -EINVAL;
    // we check if the buffer size is 0 (buf may be NULL for 0-byte reads)
    if (bytecnt == 0)
        return 0;
    // we validate buffer parameter (required for non-zero reads)
    if (!buf)
        return -EINVAL;
    // we round down to nearest block size
    unsigned long blocks_to_read = bytecnt / vbd->blksz;
    unsigned long aligned_bytecnt = blocks_to_read * vbd->blksz;
    // we truncate if reading past end (overflow-safe)
    if (pos >= sto->capacity) {
        return 0;
    }
    unsigned long long max = sto->capacity - pos;
    if (aligned_bytecnt > max) {
        aligned_bytecnt = max;
        // we re-align to block boundary
        blocks_to_read = aligned_bytecnt / vbd->blksz;
        aligned_bytecnt = blocks_to_read * vbd->blksz;
    }
    // we read block by block
    while (bytes_read < aligned_bytecnt) {
        // we acquire lock to protect concurrent access
        lock_acquire(&vbd->vioblk_lock);
        unsigned long long current_pos = pos + bytes_read;
        uint64_t sector = current_pos / 512;
        // we set up request header
        vbd->req_header.type = VIRTIO_BLK_T_IN;
        vbd->req_header.reserved = 0;
        vbd->req_header.sector = sector;
        // we set up descriptor chain (3 descriptors: header, data, status)
        // descriptor 0: header (device-readable)
        vbd->desc_table[0].addr = (uint64_t)&vbd->req_header;
        vbd->desc_table[0].len = sizeof(struct virtio_blk_req_header);
        vbd->desc_table[0].flags = VIRTQ_DESC_F_NEXT;
        vbd->desc_table[0].next = 1;
        // descriptor 1: data buffer (device-writable)
        vbd->desc_table[1].addr = (uint64_t)vbd->data_buffer;
        vbd->desc_table[1].len = vbd->blksz;
        vbd->desc_table[1].flags = VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT;
        vbd->desc_table[1].next = 2;
        // descriptor 2: status byte (device-writable)
        vbd->desc_table[2].addr = (uint64_t)&vbd->status_byte;
        vbd->desc_table[2].len = sizeof(uint8_t);
        vbd->desc_table[2].flags = VIRTQ_DESC_F_WRITE;
        vbd->desc_table[2].next = 0;
        // we add descriptor chain to available ring
        uint16_t current_avail_idx = vbd->avail_ring->idx;
        // we add the descriptor to the available ring by setting the descriptor index to 0
        vbd->avail_ring->ring[current_avail_idx % VIOBLK_QUEUE_SIZE] = 0;
        __sync_synchronize();  
        // we fence w,w to ensure the descriptor is added to the available ring
        vbd->avail_ring->idx = current_avail_idx + 1;
        // we notify the device that we have a request
        virtio_notify_avail(vbd->regs, 0);  // Queue 0
        // we wait for device to process request
        // we disable interrupts to avoid race with ISR
        int pie = disable_interrupts();
        // we wait for completion (check condition while holding lock)
        while (vbd->used_ring->idx == vbd->last_used_index) {
            // we release lock before waiting (ISR needs to be able to wake us)
            lock_release(&vbd->vioblk_lock);
            condition_wait(&vbd->request_done);
            // we re-acquire lock to check condition again
            lock_acquire(&vbd->vioblk_lock);
        }
        // we update last_used_index
        vbd->last_used_index = vbd->used_ring->idx;
        restore_interrupts(pie);
        // we check status
        if (vbd->status_byte != VIRTIO_BLK_S_OK) {
            lock_release(&vbd->vioblk_lock);
            return -EIO;
        }
        // we copy data from device buffer to user buffer
        memcpy((char*)buf + bytes_read, vbd->data_buffer, vbd->blksz);
        bytes_read += vbd->blksz;
        // we release lock before next iteration
        lock_release(&vbd->vioblk_lock);
    }
    return bytes_read;
}
// long vioblk_storage_store(struct storage * sto, unsigned long long pos, const void * buf, unsigned long bytecnt)
// Inputs: struct storage * sto - Pointer to the storage device structure
//         unsigned long long pos - Byte offset into the block device
//         const void * buf - Buffer to write from
//         unsigned long bytecnt - Number of bytes to write
// Outputs: long - Number of bytes written to device, or negative error code on failure
// Description: Writes bytecnt number of bytes from the parameter buf to the disk, rounded down to the nearest blksz. The size of the virtio device should not change. You should only overwrite existing data. Write should also not create any new files. Achieves this by filling up the block buffer and then setting the appropriate registers to request the disk write the contents of the buffer to the specified block location, truncating to avoid writing past end. Thread sleeps while waiting for the disk to service the request.
// Side Effects: Updates descriptor and available ring, notifies device, waits on condition variable, writes data from buffer to device
long vioblk_storage_store(struct storage* sto, unsigned long long pos, const void* buf,
                                 unsigned long bytecnt) {
    // we validate storage parameter
    if (!sto)
        return -EINVAL;
    struct vioblk_storage* const vbd =
        (void*)sto - offsetof(struct vioblk_storage, base);
    unsigned long bytes_written = 0;
    // we check if device is opened
    if (!vbd->opened)
        return -EINVAL;
    // we check if the buffer size is 0 (buf may be NULL for 0-byte writes)
    if (bytecnt == 0)
        return 0;
    // we validate buffer parameter (required for non-zero writes)
    if (!buf)
        return -EINVAL;
    // we round down to nearest block size
    unsigned long blocks_to_write = bytecnt / vbd->blksz;
    unsigned long aligned_bytecnt = blocks_to_write * vbd->blksz;
    // we truncate if writing past end (overflow-safe, don't extend device)
    if (pos >= sto->capacity) {
        return 0;
    }
    unsigned long long max = sto->capacity - pos;
    if (aligned_bytecnt > max) {
        aligned_bytecnt = max;
        // we re-align to block boundary
        blocks_to_write = aligned_bytecnt / vbd->blksz;
        aligned_bytecnt = blocks_to_write * vbd->blksz;
    }
    // we write block by block
    while (bytes_written < aligned_bytecnt) {
        // we acquire lock to protect concurrent access
        lock_acquire(&vbd->vioblk_lock);
        unsigned long long current_pos = pos + bytes_written;
        uint64_t sector = current_pos / 512;
        // we copy data from user buffer to device buffer
        memcpy(vbd->data_buffer, (const char*)buf + bytes_written, vbd->blksz);
        // we set up request header
        vbd->req_header.type = VIRTIO_BLK_T_OUT;
        vbd->req_header.reserved = 0;
        vbd->req_header.sector = sector;
        // we set up descriptor chain (3 descriptors: header, data, status)
        // descriptor 0: header (device-readable)
        vbd->desc_table[0].addr = (uint64_t)&vbd->req_header;
        vbd->desc_table[0].len = sizeof(struct virtio_blk_req_header);
        vbd->desc_table[0].flags = VIRTQ_DESC_F_NEXT;
        vbd->desc_table[0].next = 1;
        // descriptor 1: data buffer (device-readable for write)
        vbd->desc_table[1].addr = (uint64_t)vbd->data_buffer;
        vbd->desc_table[1].len = vbd->blksz;
        vbd->desc_table[1].flags = VIRTQ_DESC_F_NEXT;
        vbd->desc_table[1].next = 2;
        // descriptor 2: status byte (device-writable)
        vbd->desc_table[2].addr = (uint64_t)&vbd->status_byte;
        vbd->desc_table[2].len = sizeof(uint8_t);
        vbd->desc_table[2].flags = VIRTQ_DESC_F_WRITE;
        vbd->desc_table[2].next = 0;
        // we add descriptor chain to available ring
        uint16_t current_avail_idx = vbd->avail_ring->idx;
        // we add the descriptor to the available ring by setting the descriptor index to 0
        vbd->avail_ring->ring[current_avail_idx % VIOBLK_QUEUE_SIZE] = 0;
        __sync_synchronize();
        // we fence w,w to ensure the descriptor is added to the available ring
        vbd->avail_ring->idx = current_avail_idx + 1;
        // we notify the device that we have a request
        virtio_notify_avail(vbd->regs, 0);  // Queue 0
        // we wait for device to process request
        // we disable interrupts to avoid race with ISR
        int pie = disable_interrupts();
        // we wait for completion (check condition while holding lock)
        while (vbd->used_ring->idx == vbd->last_used_index) {
            // we release lock before waiting (ISR needs to be able to wake us)
            lock_release(&vbd->vioblk_lock);
            condition_wait(&vbd->request_done);
            // we re-acquire lock to check condition again
            lock_acquire(&vbd->vioblk_lock);
        }
        // we update last_used_index
        vbd->last_used_index = vbd->used_ring->idx;
        restore_interrupts(pie);
        // we check status
        if (vbd->status_byte != VIRTIO_BLK_S_OK) {
            lock_release(&vbd->vioblk_lock);
            return -EIO;
        }
        bytes_written += vbd->blksz;
        // we release lock before next iteration
        lock_release(&vbd->vioblk_lock);
    }
    return bytes_written;
}
// int vioblk_storage_cntl(struct storage * sto, int op, void * arg)
// Inputs: struct storage * sto - Pointer to the storage device structure
//         int op - Control operation to perform
//         void * arg - Argument for the operation
// Outputs: int - 0 on success, negative error code on failure
// Description: Given a storage io object, a specific command, and possibly some arguments, execute the corresponding functions on the VirtIO block device. FCNTL_GETEND returns the capacity of the VirtIO block device in bytes through the arg variable.
// Side Effects: None
int vioblk_storage_cntl(struct storage* sto, int op, void* arg) {
    if (op == FCNTL_GETEND) {
        // we return the capacity of the device in bytes
        if (arg == NULL)
            return -EINVAL;
        *(unsigned long long*)arg = sto->capacity;
        return 0;
    }
    return -ENOTSUP;
}
// void vioblk_isr(int irqno, void * aux)
// Inputs: int irqno - Interrupt source number
//         void * aux - Pointer to auxiliary data (VirtIO block device structure)
// Outputs: None (void)
// Description: VirtIO block interrupt service routine that handles device interrupts when request is complete
// Side Effects: Reads and acknowledges interrupt status by writing to interrupt_ack register
void vioblk_isr(int irqno, void* aux) {
    struct vioblk_storage* vbd = (struct vioblk_storage*)aux;
    // we read interrupt status to determine what caused the interrupt
    uint32_t interrupt_status = vbd->regs->interrupt_status;
    // we acknowledge the interrupt by writing to interrupt_ack
    vbd->regs->interrupt_ack = interrupt_status;
    // we wake up threads waiting for request completion
    condition_broadcast(&vbd->request_done);
}
