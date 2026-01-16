


/*! @file syscall.c
    @brief system call handlers
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA
*/

#ifdef SYSCALL_TRACE
#define TRACE
#endif

#ifdef SYSCALL_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "console.h"
#include "device.h"
#include "error.h"
#include "filesys.h"
#include "heap.h"
#include "intr.h"
#include "memory.h"
#include "misc.h"
#include "process.h"
#include "scnum.h"
#include "string.h"
#include "thread.h"
#include "timer.h"
#include "uio.h"

// EXPORTED FUNCTION DECLARATIONS
//

extern void handle_syscall(struct trap_frame *tfr);  // called from excp.c

// INTERNAL FUNCTION DECLARATIONS
//

static int64_t syscall(const struct trap_frame *tfr);

static int sysexit(void);
static int sysexec(int fd, int argc, char **argv);
static int sysfork(const struct trap_frame *tfr);
static int syswait(int tid);
static int sysprint(const char *msg);
static int sysusleep(unsigned long us);

static int sysfsdelete(const char *path);
static int sysfscreate(const char *path);

static int sysopen(int fd, const char *path);
static int sysclose(int fd);
static long sysread(int fd, void *buf, size_t bufsz);
static long syswrite(int fd, const void *buf, size_t len);
static int sysfcntl(int fd, int cmd, void *arg);
static int syspipe(int *wfdptr, int *rfdptr);
static int sysuiodup(int oldfd, int newfd);

// EXPORTED FUNCTION DEFINITIONS
//

/**
 * @brief Initiates syscall present in trap frame struct and stores the return address into the sepc
 * @details sepc will be used to return back to program execution after interrupt is handled and
 * sret is called
 * @param tfr pointer to trap frame struct
 * @return void
 */

void handle_syscall(struct trap_frame *tfr) {
    int64_t result;
    trace("%s()", __func__);
    // we need to call the syscall dispatcher
    result = syscall(tfr);
    // we need to store the result in the a0 register
    tfr->a0 = result;
    // we need to increment the sepc by 4 to skip the ecall instruction
    tfr->sepc = (char *)tfr->sepc + 4;
}

// INTERNAL FUNCTION DEFINITIONS
//

/**
 * @brief Calls specified syscall and passes arguments
 * @details Function uses register a7 to determine syscall number and arguments are passed in from
 * a0-a5 depending on the function
 * @param tfr pointer to trap frame struct
 * @return result of syscall
 */

int64_t syscall(const struct trap_frame *tfr) {
    long syscall_num = tfr->a7;
    trace("%s(syscall=%ld)", __func__, syscall_num);
    switch (syscall_num) {
        case SYSCALL_EXIT:
            return sysexit();
        case SYSCALL_EXEC:
            return sysexec(tfr->a0, tfr->a1, (char **)tfr->a2);
        case SYSCALL_FORK:
            return sysfork(tfr);
        case SYSCALL_WAIT:
            return syswait(tfr->a0);
        case SYSCALL_PRINT:
            return sysprint((const char *)tfr->a0);
        case SYSCALL_USLEEP:
            return sysusleep(tfr->a0);
        case SYSCALL_FSCREATE:
            return sysfscreate((const char *)tfr->a0);
        case SYSCALL_FSDELETE:
            return sysfsdelete((const char *)tfr->a0);
        case SYSCALL_OPEN:
            return sysopen(tfr->a0, (const char *)tfr->a1);
        case SYSCALL_CLOSE:
            return sysclose(tfr->a0);
        case SYSCALL_READ:
            return sysread(tfr->a0, (void *)tfr->a1, tfr->a2);
        case SYSCALL_WRITE:
            return syswrite(tfr->a0, (const void *)tfr->a1, tfr->a2);
        case SYSCALL_FCNTL:
            return sysfcntl(tfr->a0, tfr->a1, (void *)tfr->a2);
        case SYSCALL_PIPE:
            return syspipe((int *)tfr->a0, (int *)tfr->a1);
        case SYSCALL_UIODUP:
            return sysuiodup(tfr->a0, tfr->a1);
        default:
            return -ENOTSUP;
    }
}

/**
 * @brief Calls process exit
 * @return void
 */

int sysexit(void) {
    trace("%s()", __func__);
    process_exit();
    // Does not return
    return 0;
}

/**
 * @brief Executes new process given a executable and arguments
 * @details Valid fd checks, get current process struct, close fd being executed, finally calls
 * process_exec with arguments and executable io "file"
 * @param fd file descripter idx
 * @param argc number of arguments in argv
 * @param argv array of arguments for multiple args
 * @return result of process_exec, else -EBADFD on invalid file descriptors
 */

int sysexec(int fd, int argc, char **argv) {
    struct process *proc;
    struct uio *exeio;
    int result;
    int i;
    trace("%s(%d, %d, %p)", __func__, fd, argc, argv);
    // we need to get the current process
    proc = current_process();
    if (proc == NULL) {
        return -EINVAL;
    }
    // we need to validate the file descriptor
    if (fd < 0 || fd >= PROCESS_UIOMAX) {
        return -EBADFD;
    }
    // we need to check if the file descriptor is open
    if (proc->uiotab[fd] == NULL) {
        return -EBADFD;
    }
    // we need to validate the argv pointer
    result = validate_vptr(argv, (argc + 1) * sizeof(char *), PTE_R | PTE_U);
    if (result < 0) {
        return result;
    }
    // we need to validate each argument string
    for (i = 0; i < argc; i++) {
        result = validate_vstr(argv[i], PTE_U);
        if (result < 0) {
            return result;
        }
    }
    // we need to get the executable file
    exeio = proc->uiotab[fd];
    // we need to call process_exec which will close the file and does not return on success
    return process_exec(exeio, argc, argv);
}

/**
 * @brief Forks a new child process using process_fork
 * @param tfr pointer to the trap frame
 * @return result of process_fork
 */

int sysfork(const struct trap_frame *tfr) {
    trace("%s(%p)", __func__, tfr);
    return process_fork(tfr);
}

/**
 * @brief Sleeps till a specified child process completes
 * @details Calls thread_join with the thread id the process wishes to wait for
 * @param tid thread_id
 * @return result of thread_join else invalid on invalid thread id
 */

int syswait(int tid) {
    trace("%s(%d)", __func__, tid);
    return thread_join(tid);
}

/**
 * @brief Prints to console via kprintf
 * @details Validates that msg string is valid via validate_vstr and pages are mapped, calls kprintf
 * on current running process
 * @param msg string msg in userspace
 * @return 0 on sucess else error from validate_vstr
 */

int sysprint(const char *msg) {
    int result;
    trace("%s(%p)", __func__, msg);
    // we need to validate the string pointer
    result = validate_vstr(msg, PTE_U);
    if (result < 0) {
        return result;
    }
    // we need to print the message
    kprintf("%s", msg);
    return 0;
}

/**
 * @brief Sleeps process till specificed amount of time has passed
 * @details Creates alarm struct, inits struct with name usleep, which sets the current time via the
 * rd_time() function, taking values from the csr, makes frequency calcuation to determine us has
 * passed before waking process
 * @param us time in us for process to sleep
 * @return 0
 */

int sysusleep(unsigned long us) {
    struct alarm al;
    trace("%s(%lu)", __func__, us);
    // we need to initialize the alarm struct
    alarm_init(&al, "usleep");
    alarm_sleep_us(&al, us);
    return 0;
}

/**
 * @brief Creates a new file in the filesystem specified by the path.
 * @details Validates and parses the user provided path for mountpoint name, file name and calls
 * create_file.
 * @param path User provided path string.
 * @return 0 on success, negative error code if error on error.
 */

int sysfscreate(const char *path) {
    char *mpname, *flname;
    char pathbuf[256];
    int result;
    trace("%s(%p)", __func__, path);
    // we need to validate the path string
    result = validate_vstr(path, PTE_U);
    if (result < 0) {
        return result;
    }
    // we need to copy the path to the kernel buffer
    strncpy(pathbuf, path, sizeof(pathbuf) - 1);
    pathbuf[sizeof(pathbuf) - 1] = '\0';
    // we need to parse the path
    result = parse_path(pathbuf, &mpname, &flname);
    if (result < 0) {
        return result;
    }
    // we need to create the file
    return create_file(mpname, flname);
}

/**
 * @brief Deletes a file in the filesystem specified by the path.
 * @details Validates and parses the user provided path for mountpoint name, file name and calls
 * delete_file.
 * @param path User provided path string.
 * @return 0 on success, negative error code if error on error.
 */

int sysfsdelete(const char *path) {
    char *mpname, *flname;
    char pathbuf[256];
    int result;
    trace("%s(%p)", __func__, path);
    // we need to validate the path string
    result = validate_vstr(path, PTE_U);
    if (result < 0) {
        return result;
    }
    // we need to copy the path to the kernel buffer
    strncpy(pathbuf, path, sizeof(pathbuf) - 1);
    pathbuf[sizeof(pathbuf) - 1] = '\0';
    // we need to parse the path
    result = parse_path(pathbuf, &mpname, &flname);
    if (result < 0) {
        return result;
    }
    // we need to delete the file
    return delete_file(mpname, flname);
}

/**
 * @brief Opens a file or device of specified fd for given process
 * @details gets current process, allocates file descriptor (if fd = -1) or uses valid file
 * descriptor given, validates and parses user provided path, calls open_file
 * @param fd file descriptor number
 * @param path User provided path string
 * @return fd number if sucessful else return error that occured -EMFILE or -EBADFD
 */

int sysopen(int fd, const char *path) {
    struct process *proc;
    char *mpname, *flname;
    char pathbuf[256];
    int result;
    int i;
    trace("%s(%d, %p)", __func__, fd, path);
    // we need to get the current process
    proc = current_process();
    if (proc == NULL) {
        return -EINVAL;
    }
    // we need to validate the path string
    result = validate_vstr(path, PTE_U);
    if (result < 0) {
        return result;
    }
    // we need to check if the file descriptor is -1, if so, allocate a new descriptor  
    if (fd == -1) {
        for (i = 0; i < PROCESS_UIOMAX; i++) {
            if (proc->uiotab[i] == NULL) {
                fd = i;
                break;
            }
        }
        if (fd == -1) {
            return -EMFILE; 
        }
    } else {
        // we need to validate the provided file descriptor
        if (fd < 0 || fd >= PROCESS_UIOMAX) {
            return -EBADFD;
        }
        // we need to check if the file descriptor is already in use
        if (proc->uiotab[fd] != NULL) {
            return -EBUSY;
        }
    }
    // we need to copy the path to the kernel buffer
    strncpy(pathbuf, path, sizeof(pathbuf) - 1);
    pathbuf[sizeof(pathbuf) - 1] = '\0';
    // we need to parse the path
    result = parse_path(pathbuf, &mpname, &flname);
    if (result < 0) {
        return result;
    }
    // we need to open the file
    result = open_file(mpname, flname, &proc->uiotab[fd]);
    if (result < 0) {
        return result;
    }
    return fd;
}

/**
 * @brief Closes file or device of specified fd for given process
 * @details gets current process, calls close function of the io, deallocates the file descriptor
 * @param fd file descriptor
 * @return 0 on success, error on invalid file descriptor or empty file descriptor
 */

int sysclose(int fd) {
    struct process *proc;
    trace("%s(%d)", __func__, fd);
    // we need to get the current process
    proc = current_process();
    if (proc == NULL) {
        return -EINVAL;
    }
    // we need to validate the file descriptor
    if (fd < 0 || fd >= PROCESS_UIOMAX) {
        return -EBADFD;
    }
    // we need to check if the file descriptor is open
    if (proc->uiotab[fd] == NULL) {
        return -EBADFD;
    }
    // we need to close the file
    uio_close(proc->uiotab[fd]);
    proc->uiotab[fd] = NULL;
    return 0;
}

/**
 * @brief Calls read function of file io on given buffer
 * @details get current process, valid file descriptor checks, find io struct via file descriptor,
 * validate buffer, call ioread with given buffer
 * @param fd file descriptor number
 * @param buf pointer to buffer
 * @param bufsz number of bytes to be read
 * @return number of bytes read
 */

long sysread(int fd, void *buf, size_t bufsz) {
    struct process *proc;
    int result;
    trace("%s(%d, %p, %zu)", __func__, fd, buf, bufsz);
    // we need to get the current process
    proc = current_process();
    if (proc == NULL) {
        return -EINVAL;
    }
    // we need to validate the file descriptor
    if (fd < 0 || fd >= PROCESS_UIOMAX) {
        return -EBADFD;
    }
    if (fd == 2 && proc->uiotab[fd] == NULL) {
        result = open_file("dev", "uart1", &proc->uiotab[fd]);
        if (result != 0) {
            return result;
        }
    }
    // we need to check if the file descriptor is open
    if (proc->uiotab[fd] == NULL) {
        return -EBADFD;
    }
    // we need to validate the buffer
    result = validate_vptr(buf, bufsz, PTE_W | PTE_U);
    if (result < 0) {
        return result;
    }
    // we need to read from the file
    return uio_read(proc->uiotab[fd], buf, bufsz);
}

/**
 * @brief Calls write function of file io on given buffer
 * @details get current process, valid file descriptor checks, find io struct via file descriptor,
 * validate buffer, call iowrite with given buffer
 * @param fd file descriptor number
 * @param buf pointer to buffer
 * @param len number of bytes to be written
 * @return number of bytes written
 */

long syswrite(int fd, const void *buf, size_t len) {
    struct process *proc;
    int result;
    trace("%s(%d, %p, %zu)", __func__, fd, buf, len);
    // we need to get the current process
    proc = current_process();
    if (proc == NULL) {
        return -EINVAL;
    }
    // we need to validate the buffer
    result = validate_vptr(buf, len, PTE_R | PTE_U);
    if (result < 0) {
        return result;
    }
    // we need to validate the file descriptor
    if (fd < 0 || fd >= PROCESS_UIOMAX) {
        return -EBADFD;
    }
    if (fd == 2 && proc->uiotab[fd] == NULL) {
        result = open_file("dev", "uart1", &proc->uiotab[fd]);
        if (result == 0) {
            return uio_write(proc->uiotab[fd], buf, len);
        }
    }
    // we need to check if the file descriptor is open
    if (proc->uiotab[fd] == NULL) {
        return -EBADFD;
    }
    // we need to write to the file
    return uio_write(proc->uiotab[fd], buf, len);
}

/**
 * @brief Calls device input output commands for a given device instance
 * @details get current process, valid file descriptor checks, find io struct via file descriptor,
 * ensure that fcntl type exists, validate argument pointer, issue fcntl
 * @param fd file descriptor number
 * @param cmd selection of fcntl
 * @param arg pointer to arguments
 * @return number of bytes written
 */

int sysfcntl(int fd, int cmd, void *arg) {
    struct process *proc;
    int result;
    trace("%s(%d, %d, %p)", __func__, fd, cmd, arg);
    // we need to get the current process
    proc = current_process();
    if (proc == NULL) {
        return -EINVAL;
    }
    // we need to validate the file descriptor
    if (fd < 0 || fd >= PROCESS_UIOMAX) {
        return -EBADFD;
    }
    // we need to check if the file descriptor is open
    if (proc->uiotab[fd] == NULL) {
        return -EBADFD;
    }
    // we need to validate the argument pointer if not NULL 
    if (arg != NULL) {
        result = validate_vptr(arg, sizeof(unsigned long long), PTE_R | PTE_W | PTE_U);
        if (result < 0) {
            return result;
        }
    }
    // we need to issue the fcntl command
    return uio_cntl(proc->uiotab[fd], cmd, arg);
}

/**
 * @brief Creates a pipe for the current process
 * @details The function retrieves the current process. If either the write or read descriptor
 * pointer stores a negative value, an unused descriptor is assigned. If both file descriptors are
 * unused and valid, the function connects them via create_pipe function.
 * @param wfdptr pointer to write file descriptor
 * @param rfdptr pointer to read file descriptor
 * @return 0 on success. Else, negative error code on invalid file descriptor, or if a file
 * descriptor is already in use, or if no descriptors are found available.
 */
int syspipe(int *wfdptr, int *rfdptr) {
    struct process *proc;
    int wfd, rfd;
    int result;
    trace("%s(%p, %p)", __func__, wfdptr, rfdptr);
    //we need to get the current process
    proc = current_process();
    if (proc == NULL) {
        return -EINVAL;
    }
    //we need to validate the pointers
    result = validate_vptr(wfdptr, sizeof(int), PTE_W | PTE_U);
    if (result < 0) {
        return result;
    }
    //we need to validate the read file descriptor pointer
    result = validate_vptr(rfdptr, sizeof(int), PTE_W | PTE_U);
    if (result < 0) {
        return result;
    }
    //we need to read the requested descriptor numbers
    wfd = *wfdptr;
    rfd = *rfdptr;
    //we need to allocate the write descriptor if needed
    if (wfd < 0) {
        for (int i = 0; i < PROCESS_UIOMAX; i++) {
            if (proc->uiotab[i] == NULL) {
                wfd = i;
                break;
            }
        }
        if (wfd < 0) {
            return -EMFILE;
        }
    } else {
        //we need to validate the provided descriptor
        if (wfd >= PROCESS_UIOMAX) {
            return -EBADFD;
        }
        if (proc->uiotab[wfd] != NULL) {
            return -EBUSY;  
        }
    }
    //we need to allocate the read descriptor if needed
    if (rfd < 0) {
        for (int i = 0; i < PROCESS_UIOMAX; i++) {
            if (proc->uiotab[i] == NULL && i != wfd) {
                rfd = i;
                break;
            }
        }
        if (rfd < 0) {
            return -EMFILE;  
        }
    } else {
        //we need to validate the provided descriptor
        if (rfd >= PROCESS_UIOMAX) {
            return -EBADFD;
        }
        if (proc->uiotab[rfd] != NULL) {
            return -EBUSY;  
        }
        if (rfd == wfd) {
            return -EINVAL;  
        }
    }
    //we need to create the pipe
    create_pipe(&proc->uiotab[wfd], &proc->uiotab[rfd]);
    //we need to check if the pipe creation succeeded
    if (proc->uiotab[wfd] == NULL || proc->uiotab[rfd] == NULL) {
        //we need to clean up on failure
        proc->uiotab[wfd] = NULL;
        proc->uiotab[rfd] = NULL;
        return -ENOMEM;
    }
    //we need to add references to the file descriptors
    uio_addref(proc->uiotab[wfd]);
    uio_addref(proc->uiotab[rfd]);
    //we need to return the descriptor numbers to the user
    *wfdptr = wfd;
    *rfdptr = rfd;
    return 0;
}

/**
 * @brief Duplicates a file description
 * @details Allocates a new file descriptor that refers to the same open _uio_ as the descriptor
 * _oldfd_. Increments the _refcnt_ if successful.
 * @param oldfd old file descriptor number
 * @param newfd new file descriptor number
 * @return fd number if sucessful else return error on invalid file descriptor or empty file
 * descriptor
 */

int sysuiodup(int oldfd, int newfd) {
    struct process *proc;
    int i;
    trace("%s(%d, %d)", __func__, oldfd, newfd);
    // we need to get the current process
    proc = current_process();
    if (proc == NULL) {
        return -EINVAL;
    }
    // we need to validate the old file descriptor
    if (oldfd < 0 || oldfd >= PROCESS_UIOMAX) {
        return -EBADFD;
    }
    // we need to check if the old file descriptor is open
    if (proc->uiotab[oldfd] == NULL) {
        return -EBADFD;
    }
    // we need to check if the new file descriptor is -1, if so, allocate a new descriptor
    if (newfd == -1) {
        for (i = 0; i < PROCESS_UIOMAX; i++) {
            if (proc->uiotab[i] == NULL) {
                newfd = i;
                break;
            }
        }
        if (newfd == -1) {
            return -EMFILE;
        }
    } else {
        // we need to validate the new file descriptor
        if (newfd < 0 || newfd >= PROCESS_UIOMAX) {
            return -EBADFD;
        }
        // we need to check if the new file descriptor is already open, if so, close it
        if (proc->uiotab[newfd] != NULL) {
            uio_close(proc->uiotab[newfd]);
        }
    }
    // we need to duplicate the file descriptor
    proc->uiotab[newfd] = proc->uiotab[oldfd];
    uio_addref(proc->uiotab[newfd]);
    return newfd;
}
