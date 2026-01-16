/*! @file trap.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Defines the trap frame structure and related functions for handling traps and interrupts.
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-Identifier: NCSA
*/

#ifndef _TRAP_H_
#define _TRAP_H_

#include <stdint.h>

/*!
 * @brief
 * @param
 * @return
 */

// The trap_frame structure is used to save current processor state when
// handling a trap in S mode. (For handling traps to M mode, we don't bother
// with a trap frame; see see.s.)

struct trap_frame {
    long a0, a1, a2, a3, a4, a5, a6, a7;
    long t0, t1, t2, t3, t4, t5, t6;
    long s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
    void* ra;
    void* sp;
    void* gp;
    void* tp;
    long sstatus;
    unsigned long long instret;
    void* fp;    // must be here
    void* sepc;  // must be here
};

/*!
 * @brief # Restores CPU state from a trap frame as when returning to U mode.
 * @param tfr Pointer to the trap frame structure containing the state to restore.
 * @return None
 */

extern void trap_frame_jump(struct trap_frame* tfr, void* sscratch) __attribute__((noreturn));

// The following functions are called to handle interrupts and exceptions from
// trap.s. The exception handlers are defined in excp.c, and the interrupt
// handlers are defined in intr.c. Note that `handle_smode_exception` means handler
// for exceptions that occur in S mode, and `handle_umode_exception` means handler
// for exceptions that occur in U mode. In both cases, the traps are taken to
// (and handled in) S mode.

/*!
 * @brief handles the exception in S mode based on the cause.
 * @param cause The cause of the exception.
 * @param tfr Pointer to the trap frame structure containing the state at the time of the exception.
 * @return None
 */
extern void handle_smode_exception(unsigned int cause, struct trap_frame* tfr);

/*!
 * @brief handles the exception in U mode based on the cause.
 * @param cause The cause of the exception.
 * @param tfr Pointer to the trap frame structure containing the state at the time of the exception.
 * @return None
 */
extern void handle_umode_exception(unsigned int cause, struct trap_frame* tfr);

/*!
 * @brief handles the interrupt in S mode based on the cause.
 * @param cause The cause of the interrupt
 * @param Pointer to the trap frame structure containing the state at the time of the exception.
 * @return None
 */
extern void handle_smode_interrupt(unsigned int cause);

/*!
 * @brief handles the interupt in U mode based on the cause.
 * @param cause The cause of the interrupt.
 * @param tfr Pointer to the trap frame structure containing the state at the time of the exception.
 * @return None
 */
extern void handle_umode_intr(unsigned int cause);

#endif  // _TRAP_H_