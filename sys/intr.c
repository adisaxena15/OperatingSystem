
/*! @file intr.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Interrupt management
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifdef INTR_TRACE
#define TRACE
#endif

#ifdef INTR_DEBUG
#define DEBUG
#endif

#include "intr.h"

#include <stddef.h>

#include "misc.h"
#include "plic.h"
#include "riscv.h"
#include "thread.h"
#include "timer.h"
#include "trap.h"

// EXPORTED GLOBAL VARIABLE DEFINITIONS
//

char intrmgr_initialized = 0;

// INTERNAL GLOBAL VARIABLE DEFINITIONS
//

/**
 * @brief isr table, associates srnos with isrs
 */
static struct {
    void (*isr)(int, void*);  ///< isr function
    void* isr_aux;            ///< isr auxilary var
} isrtab[NIRQ];

// INTERNAL FUNCTION DECLARATIONS
//

/**
 * @brief to be called when an interrupt fires in S mode
 * @param cause Supervisor trap cause
 * @return void
 */
static void handle_interrupt(unsigned int cause);

/**
 * @brief to be called when an external fires
 * @return void
 */
static void handle_extern_interrupt(void);

// EXPORTED FUNCTION DEFINITIONS
//

/**
 * @brief initializes interrupt manager
 * @return void
 */
void intrmgr_init(void) {
    trace("%s()", __func__);

    disable_interrupts();  // should not be enabled yet
    plic_init();

    // Enable timer and external interrupts
    csrw_sie(RISCV_SIE_SEIE | RISCV_SIE_STIE);

    intrmgr_initialized = 1;
}

void enable_intr_source(int srcno, int prio, void (*isr)(int srcno, void* aux), void* isr_aux) {
    assert(0 < srcno && srcno < NIRQ);
    assert(0 < prio);

    isrtab[srcno].isr = isr;
    isrtab[srcno].isr_aux = isr_aux;
    plic_enable_source(srcno, prio);
}

void disable_intr_source(int srcno) {
    plic_disable_source(srcno);
    isrtab[srcno].isr = NULL;
    isrtab[srcno].isr_aux = NULL;
}

void handle_smode_interrupt(unsigned int cause) { handle_interrupt(cause); }

void handle_umode_interrupt(unsigned int cause) { 
    handle_interrupt(cause);
    running_thread_yield();
}

// INTERNAL FUNCTION DEFINITIONS
//

void handle_interrupt(unsigned int cause) {
    switch (cause) {
        case RISCV_SCAUSE_STI:
            handle_timer_interrupt();
            break;
        case RISCV_SCAUSE_SEI:
            handle_extern_interrupt();
            break;
        default:
            panic(NULL);
            break;
    }
}

void handle_extern_interrupt(void) {
    int srcno;

    srcno = plic_claim_interrupt();
    assert(0 <= srcno && srcno < NIRQ);

    if (srcno == 0) return;

    if (isrtab[srcno].isr == NULL) panic(NULL);

    isrtab[srcno].isr(srcno, isrtab[srcno].isr_aux);

    plic_finish_interrupt(srcno);
}
