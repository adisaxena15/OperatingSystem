/*! @file intr.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Interrupt management
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifndef _INTR_H_
#define _INTR_H_

#include "plic.h"
#include "riscv.h"

// EXPORTED CONSTANT DEFINITIONS
//

#define INTR_PRIO_MIN PLIC_PRIO_MIN
#define INTR_PRIO_MAX PLIC_PRIO_MAX
#define INTR_SRC_CNT PLIC_SRC_CNT

// EXPORTED FUNCTION DECLARATIONS
//

extern void intrmgr_init(void);
extern char intrmgr_initialized;

/**
 * @brief enables an interrupt source at the interrupt controller (e.g. PLIC)
 * @details *srcno* and *isr_aux* are arguments to *isr*
 * @param srcno source number (as known at the interrupt controller)
 * @param prio priority to be assigned to the source
 * @param isr function called by interrupt manager when the source raises an interrupt
 * @param isr_aux *isr* auxilary argument
 * @return void
 */
extern void enable_intr_source(int srcno, int prio, void (*isr)(int srcno, void* aux),
                               void* isr_aux);

/**
 * @brief disables an interrupt source at the interrupt controller (e.g. PLIC)
 * @param srcno source number (as known at the interrupt controller)
 * @return void
 */
extern void disable_intr_source(int srcno);

/**
 * @brief called when an interrupt fires in S mode
 * @details called from trap.s
 * @param cause Supervisor trap cause
 * @return void
 */
extern void handle_smode_interrupt(unsigned int cause);

/**
 * @brief enables interrupts globally
 * @return opaque value that can be passed to restore_interrupts() to restore the
 * previous interrupt enable/disable state
 */
static inline long enable_interrupts(void) { return csrrsi_sstatus_SIE(); }

/**
 * @brief disables interrupts globally
 * @return opaque value that can be passed to restore_interrupts() to restore the
 * previous interrupt enable/disable state
 */
static inline long disable_interrupts(void) { return csrrci_sstatus_SIE(); }

/**
 * @brief restores the previously saved interrupt enable/disable state.
 * @param prev_state value returned by enable_interrupts() or disable_interrupts()
 * @return void
 */
static inline void restore_interrupts(int prev_state) { csrwi_sstatus_SIE(prev_state); }

/**
 * @brief returns if interrupts are currently enabled
 * @return 1 if interrupts are currently enabled, 0 if they are currently disabled
 */
static inline int interrupts_enabled(void) { return ((csrr_sstatus() & RISCV_SSTATUS_SIE) != 0); }

/**
 * @brief returns if interrupts are currently disabled
 * @return 1 if interrupts are currently disabled, 0 if they are currently enabled
 */
static inline int interrupts_disabled(void) { return ((csrr_sstatus() & RISCV_SSTATUS_SIE) == 0); }

extern void intr_install_isr(int srcno, void (*isr)(int srcno, void* aux), void* isr_aux);
#endif  // _INTR_H_