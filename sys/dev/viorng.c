// viorng.c - VirtIO rng device
// 
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "virtio.h"
#include "intr.h"
#include "heap.h"
#include "error.h"
#include "string.h"
#include "thread.h"
#include "devimpl.h"
#include "misc.h"
#include "conf.h"
#include "intr.h"
#include "console.h"

// INTERNAL CONSTANT DEFINITIONS
//

#ifndef VIORNG_BUFSZ
#define VIORNG_BUFSZ 256
#endif

#ifndef VIORNG_NAME
#define VIORNG_NAME "viorng"
#endif

#ifndef VIORNG_IRQ_PRIO
#define VIORNG_IRQ_PRIO 1
#endif

// INTERNAL TYPE DEFINITIONS
//

// VirtIO RNG device structure

struct viorng_serial {
    struct serial base;                         
    // Base serial interface
    volatile struct virtio_mmio_regs * regs;    
    // Device registers
    int irqno;                                  
    // Interrupt number
    char opened;                                
    // Device opened flag
    struct virtq_desc desc_table[1];            
    // Descriptor table (we only need 1 descriptor for RNG)
    struct virtq_avail * avail_ring;            
    // Available ring  
    struct virtq_used * used_ring;              
    // Used ring 
    char entropy_buffer[VIORNG_BUFSZ];          
    // Buffer to receive random data from device
    uint16_t last_used_index;                   
    // Last seen used index
    struct condition data_ready;                
    // Signalled when entropy data is ready
};

// INTERNAL FUNCTION DECLARATIONS
//

static int viorng_serial_open(struct serial * ser);

static void viorng_serial_close(struct serial * ser);
static int viorng_serial_recv(struct serial * ser, void * buf, unsigned int bufsz);

static void viorng_isr(int irqno, void * aux);

// INTERNAL GLOBAL VARIABLES
//

static const struct serial_intf viorng_serial_intf = {
    .blksz = 1,
    .open = &viorng_serial_open,
    .close = &viorng_serial_close,
    .recv = &viorng_serial_recv
};

// EXPORTED FUNCTION DEFINITIONS
//

// void viorng_attach(volatile struct virtio_mmio_regs * regs, int irqno)
// Inputs: volatile struct virtio_mmio_regs * regs - Pointer to VirtIO MMIO registers
//         int irqno - Interrupt number for the device
// Outputs: None (void)
// Description: Attaches and initializes VirtIO RNG device. Negotiates features, allocates device structure, sets up virtqueue with descriptor/avail/used rings, and registers device.
// Side Effects: Allocates memory for device structure and virtqueues, initializes descriptor table, attaches virtqueue, sets device status to DRIVER_OK, registers device with device manager
void viorng_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct viorng_serial * vrng;
    int result;
    
    assert (regs->device_id == VIRTIO_ID_RNG);

    // Signal device that we found a driver

    regs->status |= VIRTIO_STAT_DRIVER;
    // fence o,io
    __sync_synchronize();

    virtio_featset_init(needed_features);
    virtio_featset_init(wanted_features);
    result = virtio_negotiate_features(regs,
        enabled_features, wanted_features, needed_features);

    if (result != 0) {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    // we allocate and initialize device struct
    vrng = kcalloc(1, sizeof(struct viorng_serial));
    vrng->regs = regs;
    vrng->irqno = irqno;
    // we set opened to 0 as the device is not opened
    vrng->opened = 0;
    // we set last_used_index to 0 as the device has not seen any used index
    vrng->last_used_index = 0;
    // we initialize condition variable
    condition_init(&vrng->data_ready, "viorng.dataready");
    // we allocate virtq_avail and virtq_used structures
    vrng->avail_ring = kcalloc(1, VIRTQ_AVAIL_SIZE(1));
    vrng->used_ring = kcalloc(1, VIRTQ_USED_SIZE(1));
    // we initialize descriptor to point to our buffer
    // we know that RNG only needs one descriptor that points to a buffer to receive random data
    vrng->desc_table[0].addr = (uint64_t)vrng->entropy_buffer;
    vrng->desc_table[0].len = VIORNG_BUFSZ;
    vrng->desc_table[0].flags = VIRTQ_DESC_F_WRITE;  
    vrng->desc_table[0].next = 0;
    // we attach the virtqueue (queue 0 is the entropy request queue and 1 is the response queue)
    virtio_attach_virtq(regs, 0, 1,
        (uint64_t)vrng->desc_table,
        (uint64_t)vrng->used_ring,
        (uint64_t)vrng->avail_ring);

    regs->status |= VIRTIO_STAT_DRIVER_OK; 
    // fence o,oi
    __sync_synchronize();

    // we initialize serial interface and register device
    serial_init(&vrng->base, &viorng_serial_intf);
    register_device(VIORNG_NAME, DEV_SERIAL, vrng);
}

// int viorng_serial_open(struct serial * ser)
// Inputs: struct serial * ser - Pointer to the serial device structure
// Outputs: int - 0 on success, -EBUSY if already opened
// Description: Opens VirtIO RNG device, enables virtqueue, and registers interrupt handler
// Side Effects: Enables virtqueue 0, registers ISR, marks device as opened
int viorng_serial_open(struct serial * ser) {
    struct viorng_serial * const device_instance =
        (void*)ser - offsetof(struct viorng_serial, base);
    // we check if already opened
    if (device_instance->opened)
        return -EBUSY;
    // we enable the virtqueue (queue 0)
    virtio_enable_virtq(device_instance->regs, 0);
    // we enable interrupt source with ISR
    enable_intr_source(device_instance->irqno, VIORNG_IRQ_PRIO, viorng_isr, device_instance);
    // we mark as opened
    device_instance->opened = 1;
    return 0;
}

// void viorng_serial_close(struct serial * ser)
// Inputs: struct serial * ser - Pointer to the serial device structure
// Outputs: None (void)
// Description: Closes VirtIO RNG device by resetting virtqueue and disabling interrupts
// Side Effects: Resets virtqueue 0, disables interrupt source, marks device as closed
void viorng_serial_close(struct serial * ser) {
    struct viorng_serial * const device_instance =
        (void*)ser - offsetof(struct viorng_serial, base);
    // we only close if currently opened
    if (!device_instance->opened)
        return;
    // we reset the virtqueue (queue 0)
    virtio_reset_virtq(device_instance->regs, 0);
    // we disable interrupt source
    disable_intr_source(device_instance->irqno);
    // we mark as closed
    device_instance->opened = 0;
}

// int viorng_serial_recv(struct serial * ser, void * buf, unsigned int bufsz)
// Inputs: struct serial * ser - Pointer to the serial device structure
//         void * buf - Pointer to buffer where random data will be stored
//         unsigned int bufsz - Size of the buffer in bytes
// Outputs: int - Number of bytes read [1, bufsz], or negative error code on failure
// Description: Reads random entropy from VirtIO RNG device using virtqueue. Submits descriptor to available ring, notifies device, and spin-waits for completion.
// Side Effects: Updates descriptor and available ring, notifies device, spin-waits on used ring, copies random data to buffer
int viorng_serial_recv(struct serial * ser, void * buf, unsigned int bufsz) {
    struct viorng_serial * const device_instance =
        (void*)ser - offsetof(struct viorng_serial, base);
    unsigned int requested_bytes;
    unsigned int actual_bytes;
    uint16_t current_avail_idx;
    // we check if device is opened
    if (!device_instance->opened)
        return -EINVAL;
    // we check if the buffer size is 0
    if (bufsz == 0)
        return 0;
    // we determine how many bytes to request (limited by our internal buffer size)
    requested_bytes = (bufsz < VIORNG_BUFSZ) ? bufsz : VIORNG_BUFSZ;
    // we update descriptor length to match requested size
    device_instance->desc_table[0].len = requested_bytes;
    // we add descriptor to available ring
    current_avail_idx = device_instance->avail_ring->idx;
    // we add the descriptor to the available ring by setting the descriptor index to 0
    device_instance->avail_ring->ring[current_avail_idx % 1] = 0;  
    __sync_synchronize();  
    // we fence w,w to ensure the descriptor is added to the available ring
    device_instance->avail_ring->idx = current_avail_idx + 1;
    // we notify the device that we have a request
    virtio_notify_avail(device_instance->regs, 0);  // Queue 0
    // we wait for device to process request
    // we disable interrupts to avoid race with ISR
    int pie = disable_interrupts();
    while (device_instance->used_ring->idx == device_instance->last_used_index) {
        condition_wait(&device_instance->data_ready);
    }
    // we update last_used_index
    device_instance->last_used_index = device_instance->used_ring->idx;
    // we get the actual number of bytes written by the device
    actual_bytes = device_instance->used_ring->ring[0].len;
    restore_interrupts(pie);
    if (actual_bytes > bufsz)
        actual_bytes = bufsz;
    // we ensure we return at least 1 byte if bufsz > 0 (as per spec)
    if (actual_bytes == 0)
        actual_bytes = 1;
    // we copy random data from our buffer to user buffer
    memcpy(buf, device_instance->entropy_buffer, actual_bytes);
    return actual_bytes;
}

// void viorng_isr(int irqno, void * aux)
// Inputs: int irqno - Interrupt source number
//         void * aux - Pointer to auxiliary data (VirtIO RNG device structure)
// Outputs: None (void)
// Description: VirtIO RNG interrupt service routine that handles device interrupts when entropy is ready
// Side Effects: Reads and acknowledges interrupt status by writing to interrupt_ack register
void viorng_isr(int irqno, void * aux) {
    struct viorng_serial * device_instance = (struct viorng_serial *)aux;
    // we read interrupt status to determine what caused the interrupt
    uint32_t interrupt_status = device_instance->regs->interrupt_status;
    // we acknowledge the interrupt by writing to interrupt_ack
    device_instance->regs->interrupt_ack = interrupt_status;
    // we wake up threads waiting for entropy data
    condition_broadcast(&device_instance->data_ready);
}