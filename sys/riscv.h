
/*! @file riscv.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief This file contains all the RISCV CSR magic numbers and functions
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA

*/

#ifndef _RISCV_H_
#define _RISCV_H_

// #include <stdint.h>

// scause

#define RISCV_SCAUSE_SSI 1
#define RISCV_SCAUSE_STI 5
#define RISCV_SCAUSE_SEI 9

#define RISCV_SCAUSE_INSTR_ADDR_MISALIGNED 0
#define RISCV_SCAUSE_INSTR_ACCESS_FAULT 1
#define RISCV_SCAUSE_ILLEGAL_INSTR 2
#define RISCV_SCAUSE_BREAKPOINT 3
#define RISCV_SCAUSE_LOAD_ADDR_MISALIGNED 4
#define RISCV_SCAUSE_LOAD_ACCESS_FAULT 5
#define RISCV_SCAUSE_STORE_ADDR_MISALIGNED 6
#define RISCV_SCAUSE_STORE_ACCESS_FAULT 7
#define RISCV_SCAUSE_ECALL_FROM_UMODE 8
#define RISCV_SCAUSE_ECALL_FROM_SMODE 9
#define RISCV_SCAUSE_INSTR_PAGE_FAULT 12
#define RISCV_SCAUSE_LOAD_PAGE_FAULT 13
#define RISCV_SCAUSE_STORE_PAGE_FAULT 15

/**
 * @brief This function retrieves scause
 * @return scause CSR value
 */
static inline long csrr_scause(void) {
    long val;
    asm("csrr %0, scause" : "=r"(val));
    return val;
}

// stval
/**
 * @brief This function retrieves stval
 * @return stval CSR value
 */
static inline unsigned long csrr_stval(void) {
    unsigned long val;
    asm("csrr %0, stval" : "=r"(val));
    return val;
}

// sepc
/**
 * @brief This function writes into stval
 * @param val new sepc value
 * @return None
 */
static inline void csrw_sepc(const void* val) { asm("csrw sepc, %0" ::"r"(val)); }

/**
 * @brief This function retrieves sepc
 * @return sepc CSR value
 */
static inline const void* csrr_sepc(void) {
    const void* val;
    asm("csrr %0, sepc" : "=r"(val));
    return val;
}

// sscratch
/**
 * @brief This function writes into sscratch
 * @param val new sscratch value
 * @return None
 */
static inline void csrw_sscratch(unsigned long val) { asm("csrw sscratch, %0" ::"r"(val)); }

/**
 * @brief This function retrieves sscratch
 * @return sepc CSR value
 */
static inline unsigned long csrr_sscratch(void) {
    unsigned long val;

    asm("csrr %0, sscratch" : "=r"(val));
    return val;
}

// stvec

#define RISCV_STVEC_MODE_shift 0
#define RISCV_STVEC_MODE_nbits 2
#define RISCV_STVEC_BASE_shift 2

#if __riscv_xlen == 32
#define RISCV_STVEC_BASE_nbits 30
#elif __riscv_xlen == 64
#define RISCV_STVEC_BASE_nbits 62
#endif

/**
 * @brief This function writes into stvec
 * @param val new stvec value
 * @return None
 */
static inline void csrw_stvec(unsigned long val) { asm("csrw stvec, %0" ::"r"(val)); }

// sie

#define RISCV_SIE_SSIE (1 << 1)
#define RISCV_SIE_STIE (1 << 5)
#define RISCV_SIE_SEIE (1 << 9)

/**
 * @brief This function overwrite sie with given mask
 * @param mask bit mask
 * @return None
 */
static inline void csrw_sie(unsigned long mask) { asm("csrw sie, %0" ::"r"(mask)); }

/**
 * @brief This function sets bits in sie provided by mask
 * @param mask bit mask
 * @return None
 */
static inline void csrs_sie(unsigned long mask) { asm("csrrs zero, sie, %0" ::"r"(mask)); }

/**
 * @brief This function clears bits in sie provided by mask
 * @param mask bit mask
 * @return None
 */
static inline void csrc_sie(unsigned long mask) { asm("csrrc zero, sie, %0" ::"r"(mask)); }

// sip

#define RV32_SIP_SSIP (1 << 1)
#define RV32_SIP_STIP (1 << 5)
#define RV32_SIP_SEIP (1 << 9)

/**
 * @brief This function overwrite sip with given mask
 * @param mask bit mask
 * @return None
 */
static inline void csrw_sip(unsigned long mask) { asm("csrw sip, %0" ::"r"(mask)); }

/**
 * @brief This function sets bits in sip provided by mask
 * @param mask bit mask
 * @return None
 */
static inline void csrs_sip(unsigned long mask) { asm("csrrs zero, sip, %0" ::"r"(mask)); }

/**
 * @brief This function clears bits in sip provided by mask
 * @param mask bit mask
 * @return None
 */
static inline void csrc_sip(unsigned long mask) { asm("csrrc zero, sip, %0" ::"r"(mask)); }

// sstatus

#define RISCV_SSTATUS_SIE (1UL << 1)
#define RISCV_SSTATUS_SPIE (1UL << 5)  // Fixed: SPIE is bit 5, not bit 3!
#define RISCV_SSTATUS_SPP (1UL << 8)
#define RISCV_SSTATUS_SUM (1UL << 18)

/**
 * @brief This function retrieves sstatus
 * @return sstatus CSR value
 */
static inline unsigned long csrr_sstatus(void) {
    unsigned long val;

    asm("csrr %0, sstatus" : "=r"(val));
    return val;
}

/**
 * @brief This function sets bits in sstatus provided by mask
 * @param mask bit mask
 * @return None
 */
static inline void csrs_sstatus(unsigned long mask) { asm("csrs sstatus, %0" ::"r"(mask)); }

/**
 * @brief This function clears bits in status provided by mask
 * @param mask bit mask
 * @return None
 */
static inline void csrc_sstatus(unsigned long mask) { asm("csrc sstatus, %0" ::"r"(mask)); }

// satp

#if __riscv_xlen == 32
#define RISCV_SATP_MODE_Sv32 1
#define RISCV_SATP_MODE_shift 31UL
#define RISCV_SATP_MODE_nbits 1
#define RISCV_SATP_ASID_shift 22UL
#define RISCV_SATP_ASID_nbits 9
#define RISCV_SATP_PPN_shift 0U
#define RISCV_SATP_PPN_nbits 22
#elif __riscv_xlen == 64
#define RISCV_SATP_MODE_Sv39 8
#define RISCV_SATP_MODE_Sv48 9
#define RISCV_SATP_MODE_Sv57 10
#define RISCV_SATP_MODE_Sv64 11
#define RISCV_SATP_MODE_shift 60UL
#define RISCV_SATP_MODE_nbits 4
#define RISCV_SATP_ASID_shift 44UL
#define RISCV_SATP_ASID_nbits 16
#define RISCV_SATP_PPN_shift 0U
#define RISCV_SATP_PPN_nbits 44
#endif

/**
 * @brief This function retrieves satp
 * @return satp CSR value
 */
static inline unsigned long csrr_satp(void) {
    unsigned long val;
    asm("csrr %0, satp" : "=r"(val));
    return val;
}

/**
 * @brief This function writes into satp
 * @param val new satp value
 * @return None
 */
static inline void csrw_satp(unsigned long val) { asm("csrw satp, %0" ::"r"(val)); }

/**
 * @brief This function reads and writes into satp atomically
 * @param new_val new satp value
 * @return old satp value that was read
 */
static inline unsigned long csrrw_satp(unsigned long new_val) {
    unsigned long prev_val;

    asm volatile("csrrw %0, satp, %1" : "=r"(prev_val) : "r"(new_val));
    return prev_val;
}

/**
 * @brief This function flushes the cached memory page table with updated value
 * @return None
 */
static inline void sfence_vma(void) { asm inline("sfence.vma" ::: "memory"); }

/**
 * @brief This function gets the value in the mtime register
 * @return value in the mtime register
 */
static inline unsigned long long rdtime(void) {
#if __riscv_xlen == 64
    unsigned long long time;
    asm("rdtime %0" : "=r"(time));
    return time;
#elif __riscv_xlen == 32
#error "rdtime() nto defined for RV32"
#endif
}

// csrrsi_sstatus_SIE() and csrrci_sstatus_SIE() set and clear sstatus.SIE. They
// return the previous value of the sstatus CSR.
/**
 * @brief This function sets SIE bit in sstatus, and returns the old value of sstatus
 * @return old sstatus that was read
 */
static inline long csrrsi_sstatus_SIE(void) {
    long sstatus;

    asm volatile("csrrsi %0, sstatus, %1" : "=r"(sstatus) : "I"(RISCV_SSTATUS_SIE));

    return sstatus;
}

/**
 * @brief This function clears SIE bit in sstatus, and returns the old value of sstatus
 * @return old sstatus that was read
 */
static inline long csrrci_sstatus_SIE(void) {
    long sstatus;

    asm volatile("csrrci %0, sstatus, %1" : "=r"(sstatus) : "I"(RISCV_SSTATUS_SIE));

    return sstatus;
}

// csrwi_sstatus_SIE() updates the SIE bit in the sstatus CSR. If the
// corresponding bit is set is _val_, then csrwi_sstatus_SIE() sets sstatus.SIE.
// Otherwise, it clears SIE. Note that there is no csrwi instruction: the _i_ is
// meant to suggest that the value is masked by RISCV_SSTATUS_SIE before being
// written to the sstatus CSR.

/**
 * @brief This function updates the SIE bit in sstatus according to new value. SIE bit value is set
 * to the value in that particular bit in the newval
 * @details csrwi_sstatus_SIE() updates the SIE bit in the sstatus CSR. If the
 * corresponding bit is set is _val_, then csrwi_sstatus_SIE() sets sstatus.SIE.
 * Otherwise, it clears SIE. Note that there is no csrwi instruction: the _i_ is
 * meant to suggest that the value is masked by RISCV_SSTATUS_SIE before being
 * written to the sstatus CSR.
 * @param newval
 */
static inline void csrwi_sstatus_SIE(long newval) {
    asm volatile(
        "csrci sstatus, %0"
        "\n\t"
        "csrs sstatus, %1" ::"I"(RISCV_SSTATUS_SIE),
        "r"(newval & RISCV_SSTATUS_SIE)
        : "memory");
}

#endif  // _RISCV_H_
