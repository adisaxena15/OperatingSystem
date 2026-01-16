// rtc.c - Goldfish RTC driver
// 
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef RTC_TRACE
#define TRACE
#endif

#ifdef RTC_DEBUG
#define DEBUG
#endif

#include "rtc.h"
#include "conf.h"
#include "misc.h"
#include "devimpl.h"
#include "console.h"
#include "string.h"
#include "heap.h"

#include "error.h"

#include <stdint.h>
#include <stddef.h>

// INTERNAL TYPE DEFINITIONS
// 

struct rtc_regs {
    uint32_t time_low;  // read first, latches time_high
    uint32_t time_high; //
};

struct rtc_device {
    struct serial base; // must be first
    volatile struct rtc_regs * regs;
};

// INTERNAL FUNCTION DEFINITIONS
//

static int rtc_open(struct serial * ser);
static void rtc_close(struct serial * ser);
static int rtc_recv(struct serial * ser, void * buf, unsigned int bufsz);

static uint64_t read_real_time(volatile struct rtc_regs * regs);

// INTERNAL GLOBAL VARIABLES AND CONSTANTS
//

static const struct serial_intf rtc_serial_intf = {
    .blksz = 8,
    .open = &rtc_open,
    .close = &rtc_close,
    .recv = &rtc_recv
};

// EXPORTED FUNCTION DEFINITIONS
// 
// void rtc_attach(void* mmio_base)
// Inputs: void* mmio_base - Pointer to the base address of the memory-mapped RTC registers
// Outputs: None (void)
// Description: Attach RTC device. This function will register the device with the system, initialize the serial device interface, and its memory-mapped registers.
// Side Effects: Allocates memory for RTC device structure, initializes serial interface, sets register base address, and registers device with system
void rtc_attach(void * mmio_base) {
    struct rtc_device * device_ptr;
    // we allocate memory for the RTC device structure
    device_ptr = kcalloc(1, sizeof(struct rtc_device));
    if (device_ptr == NULL) {
        return; // allocation failed
    }
    // we initialize the serial interface
    serial_init(&device_ptr->base, &rtc_serial_intf);
    // we set the memory-mapped register base address
    device_ptr->regs = (volatile struct rtc_regs *)mmio_base;
    // we register the device with the system
    register_device("rtc", DEV_SERIAL, &device_ptr->base);
}

int rtc_open(struct serial * ser) {
    trace("%s()", __func__);
    return 0;
}

void rtc_close(struct serial * ser) {
    trace("%s()", __func__);
}

// int rtc_recv(struct serial* ser, void* buf, unsigned int bufsz)
// Inputs: struct serial* ser - Pointer to the device serial structure
//         void* buf - Pointer to the buffer where the timestamp will be written to
//         unsigned int bufsz - Size of the buffer in bytes
// Outputs: int - Number of bytes written to buf, or negative error code on failure
// Description: Read the current time. This function gets the current real-time clock value and copies it to the given buffer. Uses offsetof macro to calculate the base address of the rtc device structure from the value of ser.
// Side Effects: Writes timestamp data to the provided buffer
int rtc_recv(struct serial * ser, void * buf, unsigned int bufsz) {
    struct rtc_device * dev_instance;
    uint64_t current_time;
    // we validate input parameters
    if (ser == NULL || buf == NULL) {
        return -EINVAL;
    }
    // if buffer is too small, return 0 (no bytes read)
    if (bufsz < sizeof(uint64_t)) {
        return 0;
    }
    // we get the rtc_device from the serial interface
    // we use offsetof to calculate the base address of rtc_device from ser
    dev_instance = (struct rtc_device *)((char *)ser - offsetof(struct rtc_device, base));
    // we read the current timestamp
    current_time = read_real_time(dev_instance->regs);
    // we copy timestamp to buffer
    memcpy(buf, &current_time, sizeof(uint64_t));
    // we return the number of bytes written
    return sizeof(uint64_t);
}

// uint64_t read_real_time(volatile struct rtc_regs* regs)
// Inputs: volatile struct rtc_regs* regs - Pointer to the memory-mapped RTC registers
// Outputs: uint64_t - The full 64-bit timestamp
// Description: Get time from registers. This helper function reads and returns the full 64-bit timestamp by combining the low and high 32-bit register values.
// Side Effects: Reads from memory-mapped RTC registers
uint64_t read_real_time(volatile struct rtc_regs * regs) {
    // we read time_low first to latch time_high value properly
    uint32_t lower_bits = regs->time_low;
    uint32_t upper_bits = regs->time_high;
    // we combine the two 32-bit values into a 64-bit timestamp
    return ((uint64_t)upper_bits << 32) | lower_bits;
}