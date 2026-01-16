
/*! @file process.h
    @brief Process management
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA

*/

#ifndef _PROCESS_H_
#define _PROCESS_H_

/*!
 * @brief Maximum number of I/O objects associated with a process
 */
#ifndef PROCESS_UIOMAX
#define PROCESS_UIOMAX 16
#endif

#include "conf.h"
#include "memory.h"
#include "thread.h"
#include "trap.h"
#include "uio.h"

// EXPORTED TYPE DEFINITIONS
//

/*!
 * @brief Process struct containing the index of the process into the proctab,
 * thread ID of the associated thread, memory space identifier of the associated
 * space and an array of I/O objects associated with the process.
 */
struct process {
    int tid;                             // thread id of our thread
    mtag_t mtag;                         // memory space
    struct uio* uiotab[PROCESS_UIOMAX];  // IO objects associated with current process
};

// EXPORTED FUNCTION DECLARATIONS
//

/*!
 * @brief Denotes whether or not the process manager has been initialized.
 * @details Initialized to 0, set to 1 on successful initialization by procmgr_init.
 */
extern char procmgr_initialized;

/*!
 * @brief Initializes the process manager as well as process 0, the process
 * associated with the main thread. Asserts the procmgr_initialized flag on
 * success.
 * @param None
 * @return None
 */
extern void procmgr_init(void);

/*!
 * @brief Executes the process associated with the specified executable I/O
 * object, argc, and argv.
 * @details Creates and builds the stack, resets the active memory space, loads
 * the process image from the given ELF, sets up the trap frame and jumps to user
 * space. On success, this function does not return. On failure, it terminates
 * the thread.
 * @param exeio Pointer to I/O struct of executable to execute
 * @param argc Number of arguments in argv
 * @param argv Array of arguments
 * @return None
 */
extern int process_exec(struct uio* exefile, int argc, char** argv);

/*!
 * @brief Forks a child process.
 * @details Creates a new process struct for the child, copies the parent's I/O
 * objects and spawns a new thread for the child. The child thread uses the
 * parent's trap frame to return to U mode, signaling the parent that it is done
 * with the trap frame.
 * @param tfr Pointer to trap frame of parent process
 * @return 0 on success, error code on failure
 */
extern int process_fork(const struct trap_frame* tfr);

#ifndef THIS_IS_ONLY_FOR_DOXYGEN
/*!
 * @brief Exits the current process. Frees the process struct, discards the
 * active memory space, closes all I/O objects, and exits the thread.
 * @param None
 * @return None
 */
extern void process_exit(void);
#endif

/*!
 * @cond IGNORED_BY_DOXYGEN
 */
// this is the real function definition
extern void __attribute__((noreturn)) process_exit(void);
/*
 * @endcond
 */

/*!
 * @brief Returns the process struct associated with the current thread.
 * @param None
 * @return Pointer to process struct associated with current thread
 */
static inline struct process* current_process(void);

// INLINE FUNCTION DEFINITIONS
//

static inline struct process* current_process(void) { return running_thread_process(); }

#endif  // _PROCESS_H_
