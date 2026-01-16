// plic.c - RISC-V PLIC
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef PLIC_TRACE
#define TRACE
#endif

#ifdef PLIC_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "plic.h"
#include "misc.h"

#include <stdint.h>

// INTERNAL MACRO DEFINITIONS
//

// CTX(i,0) is hartid /i/ M-mode context
// CTX(i,1) is hartid /i/ S-mode context

#define CTX(i,s) (2*(i)+(s))

// INTERNAL TYPE DEFINITIONS
// 


struct plic_regs {
    union {
        uint32_t priority[PLIC_SRC_CNT]; /**< Interrupt Priorities registers */
        char _reserved_priority[0x1000];
    };

    union {
        uint32_t pending[PLIC_SRC_CNT/32]; /**< Interrupt Pending Bits registers */
        char _reserved_pending[0x1000];
    };

    union {
        uint32_t enable[PLIC_CTX_CNT][32]; /**< Interrupt Enables registers */
        char _reserved_enable[0x200000-0x2000];
    };

    struct {
        union {
            struct {
                uint32_t threshold; /**< Priority Thresholds registers */
                uint32_t claim; /**< Interrupt Claim/Completion registers */
            };
            
            char _reserved_ctxctl[0x1000];
        };
    } ctx[PLIC_CTX_CNT];
};

#define PLIC (*(volatile struct plic_regs*)PLIC_MMIO_BASE)

// INTERNAL FUNCTION DECLARATIONS
//

static void plic_set_source_priority (
    uint_fast32_t srcno, uint_fast32_t level);

static int plic_source_pending(uint_fast32_t srcno);

static void plic_enable_source_for_context (
    uint_fast32_t ctxno, uint_fast32_t srcno);

static void plic_disable_source_for_context (
    uint_fast32_t ctxno, uint_fast32_t srcno);

static void plic_set_context_threshold (
    uint_fast32_t ctxno, uint_fast32_t level);

static uint_fast32_t plic_claim_context_interrupt (
    uint_fast32_t ctxno);

static void plic_complete_context_interrupt (
    uint_fast32_t ctxno, uint_fast32_t srcno);


static void plic_enable_all_sources_for_context(uint_fast32_t ctxno);

static void plic_disable_all_sources_for_context(uint_fast32_t ctxno);

// We currently only support single-hart operation, sending interrupts to S mode
// on hart 0 (context 0). The low-level PLIC functions already understand
// contexts, so we only need to modify the high-level functions (plit_init,
// plic_claim_request, plic_finish_request)to add support for multiple harts.

// EXPORTED FUNCTION DEFINITIONS
// 

void plic_init(void) {
    int i;

    // Disable all sources by setting priority to 0

    for (i = 0; i < PLIC_SRC_CNT; i++)
        plic_set_source_priority(i, 0);
    
    // Route all sources to S mode on hart 0 only

    for (int i = 0; i < PLIC_CTX_CNT; i++)
        plic_disable_all_sources_for_context(i);
    
    plic_enable_all_sources_for_context(CTX(0,1));
}

extern void plic_enable_source(int srcno, int prio) {
    trace("%s(srcno=%d,prio=%d)", __func__, srcno, prio);
    assert (0 < srcno && srcno <= PLIC_SRC_CNT);
    assert (prio > 0);

    plic_set_source_priority(srcno, prio);
}

extern void plic_disable_source(int irqno) {
    if (0 < irqno)
        plic_set_source_priority(irqno, 0);
    else
        debug("plic_disable_irq called with irqno = %d", irqno);
}

extern int plic_claim_interrupt(void) {
    trace("%s()", __func__);
    return plic_claim_context_interrupt(CTX(0,1));
}

extern void plic_finish_interrupt(int irqno) {
    trace("%s(irqno=%d)", __func__, irqno);
    plic_complete_context_interrupt(CTX(0,1), irqno);
}

// INTERNAL FUNCTION DEFINITIONS
//

// static inline void plic_set_source_priority(uint_fast32_t srcno, uint_fast32_t level)
// Inputs: uint_fast32_t srcno - Interrupt source number
//         uint_fast32_t level - Priority level to set
// Outputs: None (void)
// Description: Set the priority level for the given interrupt source
// Side Effects: Modifies PLIC priority register for the specified source
static inline void plic_set_source_priority(uint_fast32_t srcno, uint_fast32_t level) {
    // we set the priority level for the given interrupt source
    PLIC.priority[srcno] = level;
}

// static inline int plic_source_pending(uint_fast32_t srcno)
// Inputs: uint_fast32_t srcno - Interrupt source number to check
// Outputs: int - 1 if interrupt source is pending, 0 otherwise
// Description: Check if the interrupt source is pending by examining the corresponding bit
// Side Effects: Reads from PLIC pending register
static inline int plic_source_pending(uint_fast32_t srcno) {
    // we check if the interrupt source is pending by examining the corresponding bit
    uint32_t array_offset = srcno / 32; // we calculate the word index as 32 bits per word
    uint32_t position_mask = srcno % 32; // we calculate the bit index as 32 bits per word
    return (PLIC.pending[array_offset] >> position_mask) & 1;
}

// static inline void plic_enable_source_for_context(uint_fast32_t ctxno, uint_fast32_t srcno)
// Inputs: uint_fast32_t ctxno - Context number
//         uint_fast32_t srcno - Interrupt source number to enable
// Outputs: None (void)
// Description: Enable the interrupt source for the given context by setting the appropriate bit
// Side Effects: Modifies PLIC enable register for the specified context and source
static inline void plic_enable_source_for_context(uint_fast32_t ctxno, uint_fast32_t srcno) {
    // we enable the interrupt source for the given context by setting the appropriate bit
    uint32_t register_idx = srcno / 32; // we calculate the word index as 32 bits per word 
    uint32_t bit_position = srcno % 32; // we calculate the bit index as 32 bits per word
    PLIC.enable[ctxno][register_idx] |= (1U << bit_position);
}

// static inline void plic_disable_source_for_context(uint_fast32_t ctxno, uint_fast32_t srcid)
// Inputs: uint_fast32_t ctxno - Context number
//         uint_fast32_t srcid - Interrupt source number to disable
// Outputs: None (void)
// Description: Disable the interrupt source for the given context by clearing the appropriate bit
// Side Effects: Modifies PLIC enable register for the specified context and source
static inline void plic_disable_source_for_context(uint_fast32_t ctxno, uint_fast32_t srcid) {
    // we disable the interrupt source for the given context by clearing the appropriate bit
    uint32_t element_index = srcid / 32; // we calculate the word index as 32 bits per word
    uint32_t shift_amount = srcid % 32; // we calculate the bit index as 32 bits per word
    PLIC.enable[ctxno][element_index] &= ~(1U << shift_amount);
}

// static inline void plic_set_context_threshold(uint_fast32_t ctxno, uint_fast32_t level)
// Inputs: uint_fast32_t ctxno - Context number
//         uint_fast32_t level - Priority threshold level to set
// Outputs: None (void)
// Description: Set the interrupt priority threshold for the given context
// Side Effects: Modifies PLIC context threshold register
static inline void plic_set_context_threshold(uint_fast32_t ctxno, uint_fast32_t level) {
    // we set the interrupt priority threshold for the given context
    PLIC.ctx[ctxno].threshold = level;
}

// static inline uint_fast32_t plic_claim_context_interrupt(uint_fast32_t ctxno)
// Inputs: uint_fast32_t ctxno - Context number
// Outputs: uint_fast32_t - Interrupt source number of highest-priority pending interrupt
// Description: Read from the claim register to get the highest-priority pending interrupt
// Side Effects: Reads from PLIC context claim register, may atomically clear pending bit
static inline uint_fast32_t plic_claim_context_interrupt(uint_fast32_t ctxno) {
    // we read from the claim register to get the highest-priority pending interrupt
    return PLIC.ctx[ctxno].claim;
}

// static inline void plic_complete_context_interrupt(uint_fast32_t ctxno, uint_fast32_t srcno)
// Inputs: uint_fast32_t ctxno - Context number
//         uint_fast32_t srcno - Interrupt source number to complete
// Outputs: None (void)
// Description: Write the interrupt source number to the claim register to signal completion
// Side Effects: Writes to PLIC context claim register to complete interrupt handling
static inline void plic_complete_context_interrupt(uint_fast32_t ctxno, uint_fast32_t srcno) {
    // we write the interrupt source number to the claim register to signal completion
    PLIC.ctx[ctxno].claim = srcno;
}

// static void plic_enable_all_sources_for_context(uint_fast32_t ctxno)
// Inputs: uint_fast32_t ctxno - Context number
// Outputs: None (void)
// Description: Enable all interrupt sources for the given context by setting all bits
// Side Effects: Modifies all PLIC enable registers for the specified context
static void plic_enable_all_sources_for_context(uint_fast32_t ctxno) {
    // we enable all interrupt sources for the given context by setting all bits
    for (int counter = 0; counter < 32; counter++) {
        PLIC.enable[ctxno][counter] = 0xFFFFFFFF;
    }
}

// static void plic_disable_all_sources_for_context(uint_fast32_t ctxno)
// Inputs: uint_fast32_t ctxno - Context number
// Outputs: None (void)
// Description: Disable all interrupt sources for the given context by clearing all bits
// Side Effects: Modifies all PLIC enable registers for the specified context
static void plic_disable_all_sources_for_context(uint_fast32_t ctxno) {
    // we disable all interrupt sources for the given context by clearing all bits
    for (int loop_var = 0; loop_var < 32; loop_var++) {
        PLIC.enable[ctxno][loop_var] = 0;
    }
}
