// uart.c -  NS8250-compatible serial port
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef UART_TRACE
#define TRACE
#endif

#ifdef UART_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "misc.h"
#include "uart.h"
#include "devimpl.h"
#include "intr.h"
#include "heap.h"
#include "thread.h"
#include "console.h"

#include "error.h"

#include <stdint.h>

// COMPILE-TIME CONSTANT DEFINITIONS
//

#ifndef UART_RBUFSZ
#define UART_RBUFSZ 64
#endif

#ifndef UART_INTR_PRIO
#define UART_INTR_PRIO 1
#endif

#ifndef UART_DEVNAME
#define UART_DEVNAME "uart"
#endif


// INTERNAL TYPE DEFINITIONS
// 

struct uart_regs {
    union {
        char rbr; // DLAB=0 read
        char thr; // DLAB=0 write
        uint8_t dll; // DLAB=1
    };
    
    union {
        uint8_t ier; // DLAB=0
        uint8_t dlm; // DLAB=1
    };
    
    union {
        uint8_t iir; // read
        uint8_t fcr; // write
    };

    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr;
    uint8_t msr;
    uint8_t scr;
};

#define LCR_DLAB (1 << 7)
#define LSR_OE (1 << 1)
#define LSR_DR (1 << 0)
#define LSR_THRE (1 << 5)
#define IER_DRIE (1 << 0)
#define IER_THREIE (1 << 1)

// Simple fixed-size ring buffer

struct ringbuf {
    unsigned int hpos; // head of queue (from where elements are removed)
    unsigned int tpos; // tail of queue (where elements are inserted)
    char data[UART_RBUFSZ];
};

// UART device structure

struct uart_serial {
    struct serial base;
    volatile struct uart_regs * regs;
    int irqno;
    char opened;

    unsigned long rxovrcnt; ///< number of times OE was set
    
    struct condition rxbnotempty; ///< signalled when rxbuf becomes not empty
    struct condition txbnotfull;  ///< signalled when txbuf becomes not full

    struct ringbuf rxbuf;
    struct ringbuf txbuf;
};

// INTERNAL FUNCTION DEFINITIONS
//

static int uart_serial_open(struct serial * ser);
static void uart_serial_close(struct serial * ser);
static int uart_serial_recv(struct serial * ser, void * buf, unsigned int bufsz);
static int uart_serial_send(struct serial * ser, const void * buf, unsigned int bufsz);

static void uart_isr(int srcno, void * aux);

// Ring buffer (struct rbuf) functions

static void rbuf_init(struct ringbuf * rbuf);
static int rbuf_empty(const struct ringbuf * rbuf);
static int rbuf_full(const struct ringbuf * rbuf);
static void rbuf_putc(struct ringbuf * rbuf, char c);
static char rbuf_getc(struct ringbuf * rbuf);

// INTERNAL GLOBAL VARIABLES
//

static const struct serial_intf uart_serial_intf = {
    .blksz = 1,
    .open = &uart_serial_open,
    .close = &uart_serial_close,
    .recv = &uart_serial_recv,
    .send = &uart_serial_send
};

// EXPORTED FUNCTION DEFINITIONS
// 


void attach_uart(void * mmio_base, int irqno) {
    struct uart_serial * uart;

    trace("%s(%p,%d)", __func__, mmio_base, irqno);
    
    // UART0 is used for the console and should not be attached as a normal
    // device. It should already be initialized by console_init(). We still
    // register the device (to reserve the name uart0), but pass a NULL device
    // pointer, so that find_serial("uart", 0) returns NULL.

    if (mmio_base == (void*)UART0_MMIO_BASE) {
        register_device(UART_DEVNAME, DEV_SERIAL, NULL);
        return;
    }
    
    uart = kcalloc(1, sizeof(struct uart_serial));

    uart->regs = mmio_base;
    uart->irqno = irqno;
    uart->opened = 0;

    // Initialize condition variables. The ISR is registered when our interrupt
    // source is enabled in uart_serial_open().

    condition_init(&uart->rxbnotempty, "uart.rxnotempty");
    condition_init(&uart->txbnotfull, "uart.txnotfull");


    // Initialize hardware

    uart->regs->ier = 0;
    uart->regs->lcr = LCR_DLAB;
    // fence o,o ?
    uart->regs->dll = 0x01;
    uart->regs->dlm = 0x00;
    // fence o,o ?
    uart->regs->lcr = 0; // DLAB=0

    serial_init(&uart->base, &uart_serial_intf);
    register_device(UART_DEVNAME, DEV_SERIAL, uart);
}

// int uart_serial_open(struct serial * ser)
// Inputs: struct serial * ser - Pointer to the serial device structure
// Outputs: int - 0 on success, -EBUSY if already opened
// Description: Opens UART serial device, initializes buffers, enables interrupts, and registers interrupt handler
// Side Effects: Resets RX/TX buffers, flushes hardware buffer, enables data ready interrupt, registers ISR, marks device as opened
int uart_serial_open(struct serial * ser) {
    struct uart_serial * const uart =
        (void*)ser - offsetof(struct uart_serial, base);

    trace("%s()", __func__);

    if (uart->opened)
        return -EBUSY;
    
    // Reset receive and transmit buffers
    
    rbuf_init(&uart->rxbuf);
    rbuf_init(&uart->txbuf);

    // Read receive buffer register to flush any stale data in hardware buffer

    uart->regs->rbr; // forces a read because uart->regs is volatile

    // we enable interrupts when data ready (DR) status asserted
    uart->regs->ier = IER_DRIE; // Enable data ready interrupt
    // we register interrupt handler and enable interrupt source
    enable_intr_source(uart->irqno, UART_INTR_PRIO, uart_isr, uart);
    // we mark as opened
    uart->opened = 1;

    return 0;
}

// void uart_serial_close(struct serial * ser)
// Inputs: struct serial * ser - Pointer to the serial device structure
// Outputs: None (void)
// Description: Closes UART serial device by disabling interrupts
// Side Effects: Disables all device interrupts, marks device as closed
void uart_serial_close(struct serial * ser) {
    struct uart_serial * const uart =
        (void*)ser - offsetof(struct uart_serial, base);

    trace("%s()", __func__);

    // we only close if currently opened
    if (!uart->opened)
        return;
    // we disable all interrupts from device
    uart->regs->ier = 0;
    // we disable interrupt source in PLIC
    disable_intr_source(uart->irqno);
    // we mark as closed
    uart->opened = 0;
}


// int uart_serial_recv(struct serial * ser, void * buf, unsigned int bufsz)
// Inputs: struct serial * ser - Pointer to the serial device structure
//         void * buf - Pointer to buffer where received data will be stored
//         unsigned int bufsz - Size of the buffer in bytes
// Outputs: int - Number of bytes read, or negative error code on failure
// Description: Receives data from UART by reading from the receive ring buffer. Waits until requested amount of data is available.
// Side Effects: Enables data ready interrupt, reads from receive ring buffer, may block waiting for data
int uart_serial_recv(struct serial * ser, void * buf, unsigned int bufsz) {
    struct uart_serial * const uart =
        (void*)ser - offsetof(struct uart_serial, base);
    char * buffer = (char *)buf;
    unsigned int bytes_read = 0;
    // we check if device is opened
    if (!uart->opened)
        return -EINVAL;
    // we validate buffer size
    if (bufsz == 0)
        return 0;
    // we enable data ready interrupt to receive incoming data
    uart->regs->ier |= IER_DRIE;
    // we read up to bufsz bytes from receive ring buffer, limited by ring buffer size
    unsigned int max_bytes = (bufsz > UART_RBUFSZ) ? UART_RBUFSZ : bufsz;
    while (bytes_read < max_bytes) {
        // we wait for data to become available (blocking wait for CP3)
        // we disable interrupts to avoid race with ISR
        int pie = disable_interrupts();
        while (rbuf_empty(&uart->rxbuf)) {
            condition_wait(&uart->rxbnotempty);
        }
        // we copy character from ring buffer to user buffer
        buffer[bytes_read] = rbuf_getc(&uart->rxbuf);
        bytes_read++;
        restore_interrupts(pie);
    }
    return bytes_read;
}



// int uart_serial_send(struct serial * ser, const void * buf, unsigned int bufsz)
// Inputs: struct serial * ser - Pointer to the serial device structure
//         const void * buf - Pointer to buffer containing data to send
//         unsigned int bufsz - Size of the data to send in bytes
// Outputs: int - Number of bytes sent, or negative error code on failure
// Description: Sends data via UART by writing to the transmit ring buffer and enabling transmit interrupts
// Side Effects: Writes to transmit ring buffer, enables transmit interrupt, may block waiting for buffer space
int uart_serial_send(struct serial * ser, const void * buf, unsigned int bufsz) {
    struct uart_serial * const uart =
        (void*)ser - offsetof(struct uart_serial, base);
    const char * buffer = (const char *)buf;
    unsigned int bytes_sent = 0;
    // we check if device is opened
    if (!uart->opened)
        return -EINVAL;
    // we check if there is no data to send
    if (bufsz == 0)
        return 0;
    // we write up to bufsz characters to transmit ring buffer, limited by ring buffer size
    unsigned int max_bytes = (bufsz > UART_RBUFSZ) ? UART_RBUFSZ : bufsz;
    while (bytes_sent < max_bytes) {
        // Wait for space in transmit buffer (blocking wait for CP3)
        // we disable interrupts to avoid race with ISR
        int pie = disable_interrupts();
        while (rbuf_full(&uart->txbuf)) {
            condition_wait(&uart->txbnotfull);
        }
        // we copy character from user buffer to ring buffer
        rbuf_putc(&uart->txbuf, buffer[bytes_sent]);
        bytes_sent++;
        // we enable transmit interrupt to send the data
        uart->regs->ier |= IER_THREIE;
        restore_interrupts(pie);
    }
    return bytes_sent;
}

// void uart_isr(int srcno, void * aux)
// Inputs: int srcno - Interrupt source number
//         void * aux - Pointer to auxiliary data (UART device structure)
// Outputs: None (void)
// Description: UART interrupt service routine that handles receive and transmit interrupts, manages ring buffers, and handles errors
// Side Effects: Reads/writes UART registers, modifies ring buffers, updates interrupt enable register, increments error counters
void uart_isr(int srcno, void * aux) {
    struct uart_serial * device_instance = (struct uart_serial *)aux;
    uint8_t status_reg;
    // we read line status register to check UART state
    status_reg = device_instance->regs->lsr;
    // we handle receive interrupt - if data is available and receive buffer isn't full
    if ((status_reg & LSR_DR) && !rbuf_full(&device_instance->rxbuf)) {
        // we store data from RBR to receive buffer
        char incoming_byte = device_instance->regs->rbr;
        rbuf_putc(&device_instance->rxbuf, incoming_byte);
        // we wake up threads waiting for receive data
        condition_broadcast(&device_instance->rxbnotempty);
    }
    // we handle transmit interrupt - if transmit buffer isn't empty and THRE is set
    if ((status_reg & LSR_THRE) && !rbuf_empty(&device_instance->txbuf)) {
        // we write from transmit buffer to THR
        char outgoing_byte = rbuf_getc(&device_instance->txbuf);
        device_instance->regs->thr = outgoing_byte;
        // we wake up threads waiting for transmit buffer space
        condition_broadcast(&device_instance->txbnotfull);
    }
    // we disable interrupts if needed:
    // (a) we disable receive interrupt if receive buffer is full
    if (rbuf_full(&device_instance->rxbuf)) {
        device_instance->regs->ier &= ~IER_DRIE;
    }
    // (b) we disable transmit interrupt if transmit buffer is empty
    if (rbuf_empty(&device_instance->txbuf)) {
        device_instance->regs->ier &= ~IER_THREIE;
    }
    // we handle overrun error
    if (status_reg & LSR_OE) {
        device_instance->rxovrcnt++;
    }
}

void rbuf_init(struct ringbuf * rbuf) {
    rbuf->hpos = 0;
    rbuf->tpos = 0;
}



int rbuf_empty(const struct ringbuf * rbuf) {
    return (rbuf->hpos == rbuf->tpos);
}


int rbuf_full(const struct ringbuf * rbuf) {
    return (rbuf->tpos - rbuf->hpos == UART_RBUFSZ);
}


void rbuf_putc(struct ringbuf * rbuf, char c) {
    uint_fast16_t tpos;

    tpos = rbuf->tpos;
    rbuf->data[tpos % UART_RBUFSZ] = c;
    asm volatile ("" ::: "memory");
    rbuf->tpos = tpos + 1;
}

char rbuf_getc(struct ringbuf * rbuf) {
    uint_fast16_t hpos;
    char c;

    hpos = rbuf->hpos;
    c = rbuf->data[hpos % UART_RBUFSZ];
    asm volatile ("" ::: "memory");
    rbuf->hpos = hpos + 1;
    return c;
}

// The functions below provide polled uart input and output for the console.

#define UART0 (*(volatile struct uart_regs*)UART0_MMIO_BASE)

void console_device_init(void) {
    UART0.ier = 0x00;

    // Configure UART0. We set the baud rate divisor to 1, the lowest value,
    // for the fastest baud rate. In a physical system, the actual baud rate
    // depends on the attached oscillator frequency. In a virtualized system,
    // it doesn't matter.
    
    UART0.lcr = LCR_DLAB;
    UART0.dll = 0x01;
    UART0.dlm = 0x00;

    // The com0_putc and com0_getc functions assume DLAB=0.

    UART0.lcr = 0;
}

void console_device_putc(char c) {
    // Spin until THR is empty
    while (!(UART0.lsr & LSR_THRE))
        continue;

    UART0.thr = c;
}

char console_device_getc(void) {
    // Spin until RBR contains a byte
    while (!(UART0.lsr & LSR_DR))
        continue;
    
    return UART0.rbr;
}