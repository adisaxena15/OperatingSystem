
# trap.s
#
# Copyright (c) 2024-2025 University of Illinois
# SPDX-License-identifier: NCSA
#

        .text
        .global _smode_trap_entry
        .type   _smode_trap_entry, @function
        .balign 4    # Trap entry must be 4-byte aligned

        # Offsets into struct trap_frame (see trap.h)

        .equ    A0, 0*8
        .equ    A1, 1*8 
        .equ    A2, 2*8 
        .equ    A3, 3*8 
        .equ    A4, 4*8 
        .equ    A5, 5*8 
        .equ    A6, 6*8 
        .equ    A7, 7*8
        .equ    T0, 8*8
        .equ    T1, 9*8
        .equ    T2, 10*8
        .equ    T3, 11*8
        .equ    T4, 12*8
        .equ    T5, 13*8
        .equ    T6, 14*8 
        .equ    S1, 15*8
        .equ    S2, 16*8
        .equ    S3, 17*8
        .equ    S4, 18*8
        .equ    S5, 19*8
        .equ    S6, 20*8
        .equ    S7, 21*8
        .equ    S8, 22*8
        .equ    S9, 23*8
        .equ    S10, 24*8
        .equ    S11, 25*8
        .equ    RA, 26*8
        .equ    SP, 27*8
        .equ    GP, 28*8
        .equ    TP, 29*8
        .equ    SSTATUS, 30*8
        .equ    SINSTRET, 31*8
        .equ    FP, 32*8
        .equ    SEPC, 33*8
        .equ    TFRSZ, 34*8

        # struct thread_stack_anchor {
        #     struct thread * ktp;
        #     void * kgp;
        # };
        #
        # Anchor lives just above the per-thread trap frame:
        #   [ ... kernel stack ... ][ trap_frame ][ anchor ]
        #
        # For traps from U-mode, SP points at trap_frame and
        # anchor is at SP+TFRSZ.

        .equ    KTP, 0*8
        .equ    KGP, 1*8

_smode_trap_entry:

        # Swap _sp_ and _sscratch_.
        # - When coming from U mode: sscratch holds pointer to the
        #   per-thread trap frame, and sp holds the user stack pointer.
        #   After csrrw:
        #       sp       = trap_frame*
        #       sscratch = user_sp
        # - When coming from S mode: sscratch is zero, so sp becomes 0
        #   and sscratch holds the old kernel sp.

        csrrw   sp, sscratch, sp
        beqz    sp, smode_trap_from_smode

smode_trap_from_umode:
        # sp = trap_frame*, sscratch = user_sp

        # Save user stack pointer into trap_frame->sp
        csrr    t6, sscratch
        sd      t6, SP(sp)

        # We are now in S mode; sscratch is not needed while in kernel
        csrw    sscratch, zero
        j       smode_trap_save_regs

smode_trap_from_smode:
        # sp = 0, sscratch = old kernel sp

        csrr    sp, sscratch          # restore old kernel sp
        addi    sp, sp, -TFRSZ        # allocate trap frame on kernel stack

        # Save old kernel sp into trap_frame->sp (debugging / optional)
        addi    t6, sp, TFRSZ
        sd      t6, SP(sp)

        csrw    sscratch, zero

smode_trap_save_regs:
        # Save GPRs into trap frame
        sd      a0, A0(sp)
        sd      a1, A1(sp)
        sd      a2, A2(sp)
        sd      a3, A3(sp)
        sd      a4, A4(sp)
        sd      a5, A5(sp)
        sd      a6, A6(sp)
        sd      a7, A7(sp)
        sd      t0, T0(sp)
        sd      t1, T1(sp)
        sd      t2, T2(sp)
        sd      t3, T3(sp)
        sd      t4, T4(sp)
        sd      t5, T5(sp)
        sd      t6, T6(sp)
        sd      s1, S1(sp)
        sd      s2, S2(sp)
        sd      s3, S3(sp)
        sd      s4, S4(sp)
        sd      s5, S5(sp)
        sd      s6, S6(sp)
        sd      s7, S7(sp)
        sd      s8, S8(sp)
        sd      s9, S9(sp)
        sd      s10, S10(sp)
        sd      s11, S11(sp)
        sd      ra, RA(sp)
        sd      fp, FP(sp)
        sd      gp, GP(sp)
        sd      tp, TP(sp)

        # Capture retired instruction counter
        rdinstret       t6
        sd              t6, SINSTRET(sp)

        # Save sstatus and sepc CSRs
        csrr    t6, sstatus
        sd      t6, SSTATUS(sp)
        csrr    t6, sepc
        sd      t6, SEPC(sp)

        # Make fp look like a normal frame pointer
        addi    fp, sp, TFRSZ

        # Decide whether we came from U-mode or S-mode based on SPP
        ld      t6, SSTATUS(sp)
        andi    t6, t6, 0x100          # mask SPP
        beqz    t6, umode_dispatch     # SPP=0 -> came from U-mode

smode_dispatch:
        csrr    a0, scause
        mv      a1, sp                 # a1 = trap_frame*

        # Exception (scause >= 0)
        bgez    a0, smode_exception

        # Interrupt: clear MSB and call S-mode interrupt handler
        slli    a0, a0, 1
        srli    a0, a0, 1
        call    handle_smode_interrupt
        j       restore_from_trap

smode_exception:
        call    handle_smode_exception
        j       restore_from_trap

umode_dispatch:
        # Coming from U mode.
        # Fix tp and gp for kernel C code using the per-thread anchor,
        # which is located immediately above the trap frame.
        addi    t0, sp, TFRSZ          # t0 = &struct thread_stack_anchor
        ld      tp, KTP(t0)            # tp = anchor->ktp (current thread)
        ld      gp, KGP(t0)            # gp = anchor->kgp (kernel gp)

        csrr    a0, scause
        mv      a1, sp                 # a1 = trap_frame*

        # Exception (scause >= 0)
        bgez    a0, umode_exception

        # Interrupt: clear MSB and call U-mode interrupt handler
        slli    a0, a0, 1
        srli    a0, a0, 1
        call    handle_umode_interrupt
        j       restore_from_trap

umode_exception:
        call    handle_umode_exception
        j       restore_from_trap

# Both S-mode and U-mode handlers return here to restore state and sret

restore_from_trap:
        # Restore all GPRs except sp and t6 (sp handled per-mode, t6 is temp)

        ld      a0, A0(sp)
        ld      a1, A1(sp)
        ld      a2, A2(sp)
        ld      a3, A3(sp)
        ld      a4, A4(sp)
        ld      a5, A5(sp)
        ld      a6, A6(sp)
        ld      a7, A7(sp)
        ld      t0, T0(sp)
        ld      t1, T1(sp)
        ld      t2, T2(sp)
        ld      t3, T3(sp)
        ld      t4, T4(sp)
        ld      t5, T5(sp)
        ld      s1, S1(sp)
        ld      s2, S2(sp)
        ld      s3, S3(sp)
        ld      s4, S4(sp)
        ld      s5, S5(sp)
        ld      s6, S6(sp)
        ld      s7, S7(sp)
        ld      s8, S8(sp)
        ld      s9, S9(sp)
        ld      s10, S10(sp)
        ld      s11, S11(sp)
        ld      ra, RA(sp)
        ld      fp, FP(sp)
        ld      gp, GP(sp)
        ld      tp, TP(sp)

        # Decide which mode we are returning to from saved SSTATUS (SPP bit)
        ld      t0, SSTATUS(sp)
        andi    t0, t0, 0x100          # SPP
        beqz    t0, restore_to_umode   # SPP=0 -> return to U-mode

restore_to_smode:
        # Returning to S mode.
        # Restore sstatus and sepc, then pop trap frame from kernel stack.

        ld      t6, SSTATUS(sp)
        csrw    sstatus, t6

        ld      t6, SEPC(sp)
        csrw    sepc, t6

        ld      t6, T6(sp)
        addi    sp, sp, TFRSZ

        sret

restore_to_umode:
        # Returning to U mode.
        #   - sscratch must hold trap_frame* for the next trap
        #   - sp must be restored to the saved user stack pointer

        mv      t1, sp                 # t1 = trap_frame*

        ld      t6, SSTATUS(sp)
        csrw    sstatus, t6

        ld      t6, SEPC(sp)
        csrw    sepc, t6

        ld      t6, T6(sp)

        # Set sscratch = trap_frame* so that on the next trap from U
        # we can recover it with csrrw sp, sscratch, sp.
        csrw    sscratch, t1

        # Restore user stack pointer from trap_frame->sp
        ld      sp, SP(sp)

        sret

# void __attribute__ ((noreturn)) trap_frame_jump(struct trap_frame * tfr,
#                                                void *tfr_slot);
#
# Restores CPU state from a trap frame as when returning to U mode.
# On entry:
#   a0 = struct trap_frame* tfr       (in kernel memory)
#   a1 = void *tfr_slot               (location of per-thread trap frame)
#
# trap_frame_jump:
#   - sets up sstatus/sepc for U-mode,
#   - sets sscratch = tfr_slot so the next trap from U will find it,
#   - sets sp = tfr->sp and restores GPRs, then sret's to user.

        .global trap_frame_jump
        .type   trap_frame_jump, @function

trap_frame_jump:
        # Load callee-saved and temp registers from the trap frame.
        # We intentionally do NOT restore gp/tp from user-space here;
        # user code (its crt0) is responsible for setting those up.
        ld      a2, A2(a0)
        ld      a3, A3(a0)
        ld      a4, A4(a0)
        ld      a5, A5(a0)
        ld      a6, A6(a0)
        ld      a7, A7(a0)
        ld      t0, T0(a0)
        ld      t1, T1(a0)
        ld      t2, T2(a0)
        ld      t3, T3(a0)
        ld      t4, T4(a0)
        ld      t5, T5(a0)
        ld      s1, S1(a0)
        ld      s2, S2(a0)
        ld      s3, S3(a0)
        ld      s4, S4(a0)
        ld      s5, S5(a0)
        ld      s6, S6(a0)
        ld      s7, S7(a0)
        ld      s8, S8(a0)
        ld      s9, S9(a0)
        ld      s10, S10(a0)
        ld      s11, S11(a0)
        ld      ra, RA(a0)
        ld      fp, FP(a0)

        # Start from current sstatus and force:
        #   - SPP = 0 (return to U mode)
        #   - SIE = 0 in S mode
        #   - SPIE = 1 (enable interrupts in U after sret)
        csrr    t6, sstatus

        li      t0, 0x102              # bits SPP (0x100) | SIE (0x2)
        not     t0, t0                 # invert to make clear mask
        and     t6, t6, t0             # clear SPP and SIE
        ori     t6, t6, 0x20           # set SPIE bit

        csrw    sstatus, t6

        # Set sepc from trap_frame->sepc
        ld      t6, SEPC(a0)
        csrw    sepc, t6

        # sscratch must hold the per-thread trap frame slot so that on
        # the next trap from U mode, csrrw sp, sscratch, sp will recover
        # it into sp.
        csrw    sscratch, a1

        # Restore T6, SP, A1, A0 last
        ld      t6, T6(a0)
        ld      sp, SP(a0)
        ld      a1, A1(a0)
        ld      a0, A0(a0)

        sret                            # enter user mode

        .end
