
// thread.c - Threads
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

/*! @file thread.c
    @brief Thread manager and operations
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA
*/

#ifdef THREAD_TRACE
#define TRACE
#endif

#ifdef THREAD_DEBUG
#define DEBUG
#endif

#include "thread.h"

#include <stddef.h>
#include <stdint.h>

#include "misc.h"
#include "heap.h"
#include "string.h"
#include "riscv.h"
#include "intr.h"
#include "error.h"
#include "see.h"
#include "memory.h"
#include "process.h"
#include "console.h"

#include <stdarg.h>

// COMPILE-TIME PARAMETERS
//

// NTHR is the maximum number of threads

#ifndef NTHR
#define NTHR 16
#endif

// EXPORTED GLOBAL VARIABLES
//

char thrmgr_initialized = 0;

// INTERNAL TYPE DEFINITIONS
//


enum thread_state {
    THREAD_UNINITIALIZED = 0,
    THREAD_WAITING,
    THREAD_SELF,
    THREAD_READY,
    THREAD_EXITED
};

struct thread_context {
    union {
        uint64_t s[12];
        struct {
            uint64_t a[8];      // s0 .. s7
            void (*pc)(void);   // s8
            uint64_t _pad;      // s9
            void * fp;          // s10
            void * ra;          // s11
        } startup;
    };

    void * ra;
    void * sp;
};

struct thread_stack_anchor {
    struct thread * ktp;
    void * kgp;
};

struct thread {
    struct thread_context ctx;  // must be first member (thrasm.s)
    int id; // index into thrtab[]
    enum thread_state state;
    const char * name;
    struct thread_stack_anchor * stack_anchor;
    void * stack_lowest;
    struct process * proc;
    struct thread * parent;
    struct thread * list_next;
    struct condition * wait_cond;
    struct condition child_exit;
    struct lock * lock_list;
};

// INTERNAL MACRO DEFINITIONS
// 

// Pointer to running thread, which is kept in the tp (x4) register.

#define TP ((struct thread*)__builtin_thread_pointer())

// Macro for changing thread state. If compiled for debugging (DEBUG is
// defined), prints function that changed thread state.

#define set_thread_state(t,s) do { \
    debug("Thread <%s:%d> state changed from %s to %s by <%s:%d> in %s", \
        (t)->name, (t)->id, \
        thread_state_name((t)->state), \
        thread_state_name(s), \
        TP->name, TP->id, \
        __func__); \
    (t)->state = (s); \
} while (0)

// INTERNAL FUNCTION DECLARATIONS
//

// Initializes the main and idle threads. called from threads_init().

static void init_main_thread(void);
static void init_idle_thread(void);

// Sets the RISC-V thread pointer to point to a thread.

static void set_running_thread(struct thread * thr);

// Returns a string representing the state name. Used by debug and trace
// statements, so marked unused to avoid compiler warnings.

static const char * thread_state_name(enum thread_state state)
    __attribute__ ((unused));

// void thread_reclaim(int tid)
//
// Reclaims a thread's slot in thrtab and makes its parent the parent of its
// children. Frees the struct thread of the thread.

static void thread_reclaim(int tid);

// struct thread * create_thread(const char * name)
//
// Creates and initializes a new thread structure. The new thread is not added
// to any list and does not have a valid context (_thread_switch cannot be
// called to switch to the new thread).

static struct thread * create_thread(const char * name);

// void running_thread_suspend(void)
// Suspends the currently running thread and resumes the next thread on the
// ready-to-run list using _thread_swtch (in threasm.s). Must be called with
// interrupts enabled. Returns when the current thread is next scheduled for
// execution. If the current thread is TP, it is marked READY and placed
// on the ready-to-run list. Note that running_thread_suspend will only return if the
// current thread becomes READY.

static void running_thread_suspend(void);

void lock_release_completely(struct lock * lock);

// void release_all_thread_locks(struct thread * thr)
// Releases all locks held by a thread. Called when a thread exits.

static void release_all_thread_locks(struct thread * thr);

// The following functions manipulate a thread list (struct thread_list). Note
// that threads form a linked list via the list_next member of each thread
// structure. Thread lists are used for the ready-to-run list (ready_list) and
// for the list of waiting threads of each condition variable. These functions
// are not interrupt-safe! The caller must disable interrupts before calling any
// thread list function that may modify a list that is used in an ISR.

static void tlclear(struct thread_list * list);
static int tlempty(const struct thread_list * list);
static void tlinsert(struct thread_list * list, struct thread * thr);
static struct thread * tlremove(struct thread_list * list);
static void tlappend(struct thread_list * l0, struct thread_list * l1);

static void idle_thread_func(void);

// IMPORTED FUNCTION DECLARATIONS
// defined in thrasm.s
//

extern struct thread * _thread_swtch(struct thread * thr);

extern void _thread_startup(void);

// INTERNAL GLOBAL VARIABLES
//

#define MAIN_TID 0
#define IDLE_TID (NTHR-1)

static struct thread main_thread;
static struct thread idle_thread;

extern char _main_stack_lowest[]; // from start.s
extern char _main_stack_anchor[]; // from start.s

static struct thread main_thread = {
    .id = MAIN_TID,
    .name = "main",
    .state = THREAD_SELF,
    .stack_anchor = (void*)_main_stack_anchor,
    .stack_lowest = _main_stack_lowest,
    .child_exit.name = "main.child_exit"
};

extern char _idle_stack_lowest[]; // from thrasm.s
extern char _idle_stack_anchor[]; // from thrasm.s

static struct thread idle_thread = {
    .id = IDLE_TID,
    .name = "idle",
    .state = THREAD_READY,
    .parent = &main_thread,
    .stack_anchor = (void*)_idle_stack_anchor,
    .stack_lowest = _idle_stack_lowest,
    .ctx.sp = _idle_stack_anchor,
    .ctx.ra = &_thread_startup,
    .ctx.startup.pc = &idle_thread_func,// Entry function pointer
    .ctx.startup.ra = &running_thread_exit // Return address for when entry finishes
};

static struct thread * thrtab[NTHR] = {
    [MAIN_TID] = &main_thread,
    [IDLE_TID] = &idle_thread
};

static struct thread_list ready_list = {
    .head = &idle_thread,
    .tail = &idle_thread
};

// EXPORTED FUNCTION DEFINITIONS
//


int running_thread(void) {
    return TP->id;
}

void thrmgr_init(void) {
    trace("%s()", __func__);
    init_main_thread();
    init_idle_thread();
    set_running_thread(&main_thread);
    thrmgr_initialized = 1;
}

// int spawn_thread(const char * name, void (*entry)(void), ...)
// Inputs: const char * name - Name of the new thread (may be NULL)
//         void (*entry)(void) - Function pointer to entry point    
// Outputs: int - Thread ID on success, -EMTHR if max threads reached
// Description: Creates and schedules a new thread with specified entry function and arguments
// Side Effects: Allocates thread structure, adds to ready list, initializes context for entry execution
int spawn_thread (
    const char * name,
    void (*entry)(void),
    ...)
{
    struct thread * child;
    va_list ap;
    int pie;
    int i;

    child = create_thread(name);

    if (child == NULL)
        return -EMTHR;

    set_thread_state(child, THREAD_READY);

    pie = disable_interrupts();
    tlinsert(&ready_list, child);
    restore_interrupts(pie);

   // filling in entry function arguments is given below, the rest is up to you

    va_start(ap, entry);
    for (i = 0; i < 8; i++)
        child->ctx.startup.a[i] = va_arg(ap, uint64_t);
    va_end(ap);
    
    // we set stack pointer to top of allocated stack
    child->ctx.sp = child->stack_anchor;
    // we set return address to _thread_startup helper
    child->ctx.ra = &_thread_startup;
    // we set entry function pointer
    child->ctx.startup.pc = entry;
    // we set return address for when entry finishes
    child->ctx.startup.ra = &running_thread_exit;
    
    return child->id;
}

// void running_thread_exit(void)
// Inputs: None
// Outputs: None (void) - Does not return
// Description: Terminates the currently running thread and signals parent if waiting
// Side Effects: Sets thread state to EXITED, signals parent, releases locks, calls halt_success for main thread
void running_thread_exit(void) {
    // we check if this is the main thread
    if (TP->id == MAIN_TID) {
        halt_success();
    }
    // we release all locks held by this thread
    release_all_thread_locks(TP);
    // we set the current thread's state to EXITED
    set_thread_state(TP, THREAD_EXITED);
    // we signal the parent thread in case it is waiting for us to exit
    if (TP->parent != NULL) {
        condition_broadcast(&TP->parent->child_exit);
    }
    // we suspend this thread (should not return)
    running_thread_suspend();
    // we should never reach here
    halt_failure();
}


void running_thread_yield(void) {
    trace("%s() in <%s:%d>", __func__, TP->name, TP->id);
    running_thread_suspend();
}


// int thread_join(int tid)
// Inputs: int tid - Thread ID to wait for (0 = wait for any child)
// Outputs: int - Thread ID of exited child on success, -EINVAL on error
// Description: Waits for a child thread to exit and reclaims its resources
// Side Effects: Blocks until child exits, signals parent when child terminates, frees child resources
int thread_join(int tid) {
    struct thread * target_thread;
    int thread_index;
    if (tid != 0) {
        // we wait for a specific child
        // we check if thread exists
        if (tid < 0 || tid >= NTHR || thrtab[tid] == NULL)
            return -EINVAL;
        target_thread = thrtab[tid];
        // we check if it's our child
        if (target_thread->parent != TP)
            return -EINVAL;
        // we wait for child to exit if it hasn't already
        while (target_thread->state != THREAD_EXITED) {
            // we wait for the child to exit
            condition_wait(&TP->child_exit);
        }
        // we reclaim the child's resources
        thread_reclaim(tid);
        return tid;
    } else {
        // we wait for any child
        // we first check if we have any children
        int has_children = 0;
        for (thread_index = 1; thread_index < NTHR; thread_index++) {
            if (thrtab[thread_index] != NULL && thrtab[thread_index]->parent == TP) {
                has_children = 1;
                // we break out of the loop once we find a child
                break;
            }
        }
        if (!has_children)
            return -EINVAL;
        // we look for an already-exited child
        for (thread_index = 1; thread_index < NTHR; thread_index++) {
            if (thrtab[thread_index] != NULL && thrtab[thread_index]->parent == TP && 
                thrtab[thread_index]->state == THREAD_EXITED) {
                // we reclaim the child's resources
                thread_reclaim(thread_index);
                return thread_index;
            }
        }
        // we wait for a child to exit
        condition_wait(&TP->child_exit);
        // we find which child exited
        for (thread_index = 1; thread_index < NTHR; thread_index++) {
            if (thrtab[thread_index] != NULL && thrtab[thread_index]->parent == TP && 
                thrtab[thread_index]->state == THREAD_EXITED) {
                thread_reclaim(thread_index);
                return thread_index;
            }
        }
        // we should never reach here
        return -EINVAL;
    }
}

struct process * thread_process(int tid) {
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    return thrtab[tid]->proc;
}

struct process * running_thread_process(void) {
    return TP->proc;
}

void thread_set_process(int tid, struct process * proc) {
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    thrtab[tid]->proc = proc;
}

void thread_detach(int tid) {
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    thrtab[tid]->parent = NULL;
}

const char * thread_name(int tid) {
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    return thrtab[tid]->name;
}

const char * running_thread_name(void) {
    return TP->name;
}

void * running_thread_stack_base(void){
    return TP->stack_anchor;
}

void condition_init(struct condition * cond, const char * name) {
    tlclear(&cond->wait_list);
    cond->name = name;
}

void condition_wait(struct condition * cond) {
    int pie;

    trace("%s(cond=<%s>) in <%s:%d>", __func__,
        cond->name, TP->name, TP->id);

    assert(TP->state == THREAD_SELF);

    // Insert current thread into condition wait list
    
    set_thread_state(TP, THREAD_WAITING);
    TP->wait_cond = cond;
    TP->list_next = NULL;

    pie = disable_interrupts();
    tlinsert(&cond->wait_list, TP);
    restore_interrupts(pie);

    running_thread_suspend();
}

// void condition_broadcast(struct condition * cond)
// Inputs: struct condition * cond - Pointer to condition variable
// Outputs: None (void)
// Description: Wakes up all threads waiting on the condition variable
// Side Effects: Changes thread states from WAITING to READY, moves threads to ready list, empties wait list
void condition_broadcast(struct condition * cond) {
    struct thread * current_thread;
    int interrupt_state;
    trace("%s(cond=<%s>)", __func__, cond->name);
    // we disable interrupts to safely modify thread lists
    interrupt_state = disable_interrupts();
    // we wake up all threads waiting on this condition
    current_thread = cond->wait_list.head;
    while (current_thread != NULL) {
        // we change thread state from WAITING to READY
        set_thread_state(current_thread, THREAD_READY);
        // we clear the wait_cond pointer
        current_thread->wait_cond = NULL;
        // we move to next thread
        current_thread = current_thread->list_next;
    }
    // we move all threads from condition's wait_list to ready_list
    tlappend(&ready_list, &cond->wait_list);
    // we restore interrupt state
    restore_interrupts(interrupt_state);
}

void lock_init(struct lock * lock) {
    memset(lock, 0, sizeof(struct lock));
    condition_init(&lock->release, "lock_release");
}

void lock_acquire(struct lock * lock) {
    if (lock->owner != TP) {
        while (lock->owner != NULL)
            condition_wait(&lock->release);
        
        lock->owner = TP;
        lock->cnt = 1;
        lock->next = TP->lock_list;
        TP->lock_list = lock;
    } else
        lock->cnt += 1;
}

void lock_release(struct lock * lock) {
    assert (lock->owner == TP);
    assert (lock->cnt != 0);

    lock->cnt -= 1;

    if (lock->cnt == 0)
        lock_release_completely(lock);
}

// INTERNAL FUNCTION DEFINITIONS
//

void init_main_thread(void) {
    main_thread.stack_anchor->ktp = &main_thread;
    void *gpp;
    asm volatile("mv %0, gp" : "=r"(gpp));
    main_thread.stack_anchor->kgp = gpp;
}

void init_idle_thread(void) {
    idle_thread.stack_anchor->ktp = &idle_thread;
    void *gpp;
    asm volatile("mv %0, gp" : "=r"(gpp));
    idle_thread.stack_anchor->kgp = gpp;
}

static void set_running_thread(struct thread * thr) {
    asm inline ("mv tp, %0" :: "r"(thr) : "tp");
}

const char * thread_state_name(enum thread_state state) {
    static const char * const names[] = {
        [THREAD_UNINITIALIZED] = "UNINITIALIZED",
        [THREAD_WAITING] = "WAITING",
        [THREAD_SELF] = "SELF",
        [THREAD_READY] = "READY",
        [THREAD_EXITED] = "EXITED"
    };

    if (0 <= (int)state && (int)state < sizeof(names)/sizeof(names[0]))
        return names[state];
    else
        return "UNDEFINED";
};

void thread_reclaim(int tid) {
    struct thread * const thr = thrtab[tid];
    int ctid;

    assert (0 < tid && tid < NTHR && thr != NULL);
    assert (thr->state == THREAD_EXITED);

    // Make our parent thread the parent of our child threads. We need to scan
    // all threads to find our children. We could keep a list of all of a
    // thread's children to make this operation more efficient.

    for (ctid = 1; ctid < NTHR; ctid++) {
        if (thrtab[ctid] != NULL && thrtab[ctid]->parent == thr)
            thrtab[ctid]->parent = thr->parent;
    }

    thrtab[tid] = NULL;
    kfree(thr);
}

struct thread * create_thread(const char * name) {
    struct thread_stack_anchor * anchor;
    void * stack_lowest;
    size_t stack_size;
    struct thread * thr;
    int tid;

    trace("%s(name=\"%s\") in <%s:%d>", __func__, name, TP->name, TP->id);

    // Find a free thread slot.

    tid = 0;
    while (++tid < NTHR)
        if (thrtab[tid] == NULL)
            break;
    
    if (tid == NTHR)
        return NULL;
    
    // Allocate a struct thread and a stack

    thr = kcalloc(1, sizeof(struct thread));
    if (thr == NULL)
        return NULL;
    
    stack_size = PAGE_SIZE;
    stack_lowest = alloc_phys_page();
    if (stack_lowest == NULL) {
        kfree(thr);
        return NULL;
    }
    anchor = stack_lowest + stack_size;
    anchor -= 1; // anchor is at base of stack
    thr->stack_lowest = stack_lowest;
    thr->stack_anchor = anchor;
    anchor->ktp = thr;
    void *gpp;
    asm volatile("mv %0, gp" : "=r"(gpp));
    anchor->kgp = gpp;

    thrtab[tid] = thr;

    thr->id = tid;
    thr->name = name;
    thr->parent = TP;
    thr->proc = TP->proc;
    return thr;
}

// void running_thread_suspend(void)
// Inputs: None
// Outputs: None (void)
// Description: Suspends current thread and switches to next ready thread using round-robin scheduling
// Side Effects: Adds current thread to ready list if runnable, switches context, frees exited thread stacks
void running_thread_suspend(void) {
    struct thread * next_thread;
    struct thread * previous_thread;
    int interrupt_state;
    trace("%s() in <%s:%d>", __func__, TP->name, TP->id);
    // we disable interrupts for critical section
    interrupt_state = disable_interrupts();
    // we add current thread back to ready list if it's still runnable
    if (TP->state == THREAD_SELF) {
        set_thread_state(TP, THREAD_READY);
        tlinsert(&ready_list, TP);
    }
    // we get the next thread from the ready list
    next_thread = tlremove(&ready_list);
    assert(next_thread != NULL);
    // we set next thread to running state
    set_thread_state(next_thread, THREAD_SELF);
    // we save the previous thread pointer before context switch
    previous_thread = TP;
    // we enable interrupts before context switch
    restore_interrupts(interrupt_state);
    // we switch memory space if next thread is a user thread
    if (next_thread->proc != NULL) {
        switch_mspace(next_thread->proc->mtag);
    }
    // we switch to the next thread
    previous_thread = _thread_swtch(next_thread);
    // we check if the thread that just ran is exited and free its stack
    if (previous_thread->state == THREAD_EXITED) {
        // we free the exited thread's stack
        free_phys_page(previous_thread->stack_lowest);
    }
}

void tlclear(struct thread_list * list) {
    list->head = NULL;
    list->tail = NULL;
}

int tlempty(const struct thread_list * list) {
    return (list->head == NULL);
}

void tlinsert(struct thread_list * list, struct thread * thr) {
    thr->list_next = NULL;

    if (thr == NULL)
        return;

    if (list->tail != NULL) {
        assert (list->head != NULL);
        list->tail->list_next = thr;
    } else {
        assert(list->head == NULL);
        list->head = thr;
    }

    list->tail = thr;
}

struct thread * tlremove(struct thread_list * list) {
    struct thread * thr;

    thr = list->head;
    
    if (thr == NULL)
        return NULL;

    list->head = thr->list_next;
    
    if (list->head != NULL)
        thr->list_next = NULL;
    else
        list->tail = NULL;

    thr->list_next = NULL;
    return thr;
}

void tlappend(struct thread_list * l0, struct thread_list * l1) {
    if (l0->head != NULL) {
        assert(l0->tail != NULL);
        
        if (l1->head != NULL) {
            assert(l1->tail != NULL);
            l0->tail->list_next = l1->head;
            l0->tail = l1->tail;
        }
    } else {
        assert(l0->tail == NULL);
        l0->head = l1->head;
        l0->tail = l1->tail;
    }

    l1->head = NULL;
    l1->tail = NULL;
}

void lock_release_completely(struct lock * lock) {
    struct lock ** hptr;

    condition_broadcast(&lock->release);
    hptr = &TP->lock_list;
    while (*hptr != lock && *hptr != NULL)
        hptr = &(*hptr)->next;
    assert (*hptr != NULL);
    *hptr = (*hptr)->next;
    lock->owner = NULL;
    lock->next = NULL;
}

void release_all_thread_locks(struct thread * thr) {
    struct lock * head;
    struct lock * next;

    head = thr->lock_list;

    while (head != NULL) {
        next = head->next;
        head->next = NULL;
        head->owner = NULL;
        head->cnt = 0;
        condition_broadcast(&head->release);
        head = next;
    }

    thr->lock_list = NULL;
}

void idle_thread_func(void) {
    // The idle thread sleeps using wfi if the ready list is empty. Note that we
    // need to disable interrupts before checking if the thread list is empty to
    // avoid a race condition where an ISR marks a thread ready to run between
    // the call to tlempty() and the wfi instruction.

    for (;;) {
        // If there are runnable threads, yield to them.

        while (!tlempty(&ready_list))
            running_thread_yield();
        
        // No runnable threads. Sleep using the wfi instruction. Note that we
        // need to disable interrupts and check the runnable thread list one
        // more time (make sure it is empty) to avoid a race condition where an
        // ISR marks a thread ready before we call the wfi instruction.

        disable_interrupts();
        if (tlempty(&ready_list))
            asm ("wfi");
        enable_interrupts();
    }
}
