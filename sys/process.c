

/*! @file process.c
    @brief user process
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA

*/

/*!
 * @brief Enables trace messages for process.c
 */
#ifdef PROCESS_TRACE
#define TRACE
#endif

/*!
 * @brief Enables debug messages for process.c
 */
#ifdef PROCESS_DEBUG
#define DEBUG
#endif

#include "process.h"

#include "conf.h"
#include "console.h"
#include "elf.h"
#include "error.h"
#include "filesys.h"
#include "heap.h"
#include "memory.h"
#include "misc.h"
#include "riscv.h"
#include "string.h"
#include "thread.h"
#include "trap.h"
#include "uio.h"

// COMPILE-TIME PARAMETERS
//

/*!
 * @brief Maximum number of processes
 */
#ifndef NPROC
#define NPROC 16
#endif

// INTERNAL FUNCTION DECLARATIONS
//

static int build_stack(void* stack, int argc, char** argv);

static void fork_func(struct condition* forked, struct trap_frame* tfr);

// INTERNAL GLOBAL VARIABLES
//

/*!
 * @brief The main user process struct
 */
static struct process main_proc;

static struct process* proctab[NPROC] = {&main_proc};

// EXPORTED GLOBAL VARIABLES
//

char procmgr_initialized = 0;

// EXPORTED FUNCTION DEFINITIONS
//

void procmgr_init(void) {
    assert(memory_initialized && heap_initialized);
    assert(!procmgr_initialized);

    main_proc.tid = running_thread();
    main_proc.mtag = active_mspace();
    thread_set_process(main_proc.tid, &main_proc);
    main_proc.uiotab[0] = create_null_uio();
    uio_addref(main_proc.uiotab[0]);
    procmgr_initialized = 1;
}

int process_exec(struct uio* exefile, int argc, char** argv) {
    struct process* proc;
    void* stack_page;
    int stksz;
    void (*entry)(void);
    struct trap_frame tfr;
    void* sscratch;
    int result;
    trace("%s(%p, %d, %p)", __func__, exefile, argc, argv);
    if (exefile == NULL) {
        return -EINVAL;
    }
    // we need to get the current process
    proc = current_process();
    if (proc == NULL) {
        return -EINVAL;
    }
    // we need to allocate and map a stack page at the top of user memory
    stack_page = alloc_phys_page();
    if (stack_page == NULL) {
        process_exit();
    }
    // we need to build the stack with argc and argv
    stksz = build_stack(stack_page, argc, argv);
    if (stksz < 0) {
        process_exit();
    }
    // we need to reset the active memory space (unmaps all non-global user pages)
    reset_active_mspace();
    map_page(UMEM_END_VMA - PAGE_SIZE, stack_page, PTE_R | PTE_W | PTE_U);
    // we need to load the ELF executable
    result = elf_load(exefile, &entry);
    if (result < 0) {
        process_exit();
    }
    // we need to find and clear the fd that points to exefile (BEFORE closing)
    for (int i = 0; i < PROCESS_UIOMAX; i++) {
        if (proc->uiotab[i] == exefile) {
            proc->uiotab[i] = NULL;
            break;
        }
    }
    // we need to close the executable file
    uio_close(exefile);
    // we need to set up the trap frame for jumping to user mode
    memset(&tfr, 0, sizeof(tfr));
    // we need to set the entry point
    tfr.sepc = entry;
    // we need to set the stack pointer (points to top of stack - stack size)
    tfr.sp = (void *)(UMEM_END_VMA - stksz);
    // we need to set the argc and argv for user program
    tfr.a0 = argc;
    tfr.a1 = (long)(UMEM_END_VMA - stksz);
    // we need to set up the sstatus: SPP=0 (user mode), SPIE=1 (enable interrupts)
    tfr.sstatus = RISCV_SSTATUS_SPIE;
    tfr.sstatus &= ~RISCV_SSTATUS_SPP; 
    // we need to calculate the sscratch (thread stack anchor - trap frame size)
    void *stack_base = running_thread_stack_base();
    void *tfr_slot = (char *)stack_base - sizeof(struct trap_frame);
    // Flush TLB to ensure page table changes are visible
    sfence_vma();
    sscratch = tfr_slot;
    // we need to jump to user mode (this does not return)
    trap_frame_jump(&tfr, sscratch);
}

int process_fork(const struct trap_frame* tfr) {
    struct process* parent_proc;
    struct process* child_proc;
    struct trap_frame* child_tfr;
    struct condition forked;
    mtag_t child_mtag;
    int child_tid;
    int i;
    trace("%s(%p)", __func__, tfr);
    //we need to get the current (parent) process
    parent_proc = current_process();
    if (parent_proc == NULL) {
        return -EINVAL;
    }
    //we need to clone the address space
    child_mtag = clone_active_mspace();
    if (child_mtag == 0) {
        return -ENOMEM;
    }
    //we need to allocate a new process struct for the child
    child_proc = kcalloc(1, sizeof(struct process));
    if (child_proc == NULL) {
        // Free the cloned memory space
        mtag_t saved_mtag = switch_mspace(child_mtag);
        discard_active_mspace();
        switch_mspace(saved_mtag);
        return -ENOMEM;
    }
    //we need to set up the child process struct
    child_proc->mtag = child_mtag;
    //we need to copy file descriptors from parent to child and increment reference counts
    for (i = 0; i < PROCESS_UIOMAX; i++) {
        if (parent_proc->uiotab[i] != NULL) {
            child_proc->uiotab[i] = parent_proc->uiotab[i];
            uio_addref(child_proc->uiotab[i]);
        } else {
            child_proc->uiotab[i] = NULL;
        }
    }
    //we need to make a copy of the trap frame for the child
    child_tfr = kmalloc(sizeof(struct trap_frame));
    if (child_tfr == NULL) {
        // Clean up: close all UIOs, free child_proc, and free cloned memory space
        for (i = 0; i < PROCESS_UIOMAX; i++) {
            if (child_proc->uiotab[i] != NULL) {
                uio_close(child_proc->uiotab[i]);
            }
        }
        kfree(child_proc);
        mtag_t saved_mtag = switch_mspace(child_mtag);
        discard_active_mspace();
        switch_mspace(saved_mtag);
        return -ENOMEM;
    }
    memcpy(child_tfr, tfr, sizeof(struct trap_frame));
    //we need to initialize the condition variable for synchronization
    condition_init(&forked, "forked");
    //we need to spawn a new thread for the child that will execute fork_func
    child_tid = spawn_thread("fork_child", (void (*)(void))fork_func, 
                            (uint64_t)&forked, (uint64_t)child_tfr);
    if (child_tid < 0) {
        //we need to clean up on failure: free trap frame, close UIOs, free child_proc, and free cloned memory space
        kfree(child_tfr);
        for (i = 0; i < PROCESS_UIOMAX; i++) {
            if (child_proc->uiotab[i] != NULL) {
                uio_close(child_proc->uiotab[i]);
            }
        }
        kfree(child_proc);
        mtag_t saved_mtag = switch_mspace(child_mtag);
        discard_active_mspace();
        switch_mspace(saved_mtag);
        return child_tid;
    }
    //we need to set the child process's tid
    child_proc->tid = child_tid;
    //we need to associate the child thread with the child process
    thread_set_process(child_tid, child_proc);
    //we need to wait for the child to signal that it's done with the trap frame
    condition_wait(&forked);
    //we need to free the trap frame copy (child is done with it)
    kfree(child_tfr);
    //we need to return the child thread ID to the parent
    return child_tid;
}

/** \brief
 *
 *
 *  Discard memory space, close your associated uio, free the memory you're supposed to free.
 *
 *
 */
void process_exit(void) {
    struct process* proc;
    int i;
    trace("%s()", __func__);
    // we need to get the current process
    proc = current_process();
    if (proc == NULL) {
        running_thread_exit();
    }
    // we need to close all open UIO objects
    for (i = 0; i < PROCESS_UIOMAX; i++) {
        if (proc->uiotab[i] != NULL) {
            uio_close(proc->uiotab[i]);
            proc->uiotab[i] = NULL;
        }
    }
    // we need to discard the active memory space (switches to main and frees non-global pages)
    discard_active_mspace();
    // we need to exit the thread (this does not return)
    running_thread_exit();
}

// INTERNAL FUNCTION DEFINITIONS
//

/**
 * \brief Builds the initial user stack for a new process.
 *
 * Builds the stack for a new process, including the argument vector (\p argv)
 * and the strings it points to. Note that \p argv must contain \p argc + 1
 * elements (the last one is a NULL pointer).
 *
 * Remember to round the final stack size up to a multiple of 16 bytes
 * (RISC-V ABI requirement).
 *
 * \param[in,out] stack  Pointer to the stack page (destination buffer).
 * \param[in]     argc   Number of arguments in \p argv.
 * \param[in]     argv   Array of argument pointers; length is \p argc+1 and
 *                       \p argv[argc] must be NULL.
 *
 * \return Size of the stack page on success; negative error code on failure.
 */
int build_stack(void* stack, int argc, char** argv) {
    size_t stksz, argsz;
    uintptr_t* newargv;
    char* p;
    int i;

    // We need to be able to fit argv[] on the initial stack page, so _argc_
    // cannot be too large. Note that argv[] contains argc+1 elements (last one
    // is a NULL pointer).

    if (PAGE_SIZE / sizeof(char*) - 1 < argc) return -ENOMEM;

    stksz = (argc + 1) * sizeof(char*);

    // Add the sizes of the null-terminated strings that argv[] points to.

    for (i = 0; i < argc; i++) {
        argsz = strlen(argv[i]) + 1;
        if (PAGE_SIZE - stksz < argsz) return -ENOMEM;
        stksz += argsz;
    }

    // Round up stksz to a multiple of 16 (RISC-V ABI requirement).

    stksz = ROUND_UP(stksz, 16);
    assert(stksz <= PAGE_SIZE);

    // Set _newargv_ to point to the location of the argument vector on the new
    // stack and set _p_ to point to the stack space after it to which we will
    // copy the strings. Note that the string pointers we write to the new
    // argument vector must point to where the user process will see the stack.
    // The user stack will be at the highest page in user memory, the address of
    // which is `(UMEM_END_VMA - PAGE_SIZE)`. The offset of the _p_ within the
    // stack is given by `p - newargv'.

    newargv = stack + PAGE_SIZE - stksz;
    p = (char*)(newargv + argc + 1);

    for (i = 0; i < argc; i++) {
        newargv[i] = (UMEM_END_VMA - PAGE_SIZE) + ((void*)p - (void*)stack);
        argsz = strlen(argv[i]) + 1;
        memcpy(p, argv[i], argsz);
        p += argsz;
    }

    newargv[argc] = 0;
    return stksz;
}

/**
 * \brief Function to be executed by the child process after fork.
 * This is a very beautiful function.
 * Tell the parent process that it is done with the trap frame, then jumps to user space (hint:
 * which function should we use?)
 *
 * \param[in] done  Pointer to a condition variable to signal parent
 * \param[in] tfr   Pointer to a trap frame
 *
 * \return NONE (very important, this is a hint)
 */
void fork_func(struct condition* done, struct trap_frame* tfr) {
    void* sscratch;
    trace("%s(%p, %p)", __func__, done, tfr);
    //we need to set the return value to 0 for the child process
    tfr->a0 = 0;
    //we need to advance sepc by 4 to skip past the ecall instruction
    tfr->sepc = (char*)tfr->sepc + 4;
    //we need to switch to the child's memory space before entering user mode
    struct process* child_proc = current_process();
    if (child_proc != NULL) {
        switch_mspace(child_proc->mtag);
    }
    //we need to calculate sscratch: pointer to per-thread trap frame slot
    void* stack_base = running_thread_stack_base();
    void* tfr_slot = (char*)stack_base - sizeof(struct trap_frame);
    sscratch = tfr_slot;
    //we need to signal the parent that we're done with the trap frame
    condition_broadcast(done);
    //we need to jump to user mode using the trap frame (does not return)
    trap_frame_jump(tfr, sscratch);
}
