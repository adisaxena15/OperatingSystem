/*! @file uio.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Uniform I/O interface
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifndef _UIO_H_
#define _UIO_H_

#include <stddef.h>  // for size_t

struct uio;  // opaque decl.

/**
 * @brief Returns reference count of passed uio struct
 * @param uio Pointer to uio struct to check reference count on
 * @return Reference count of passed uio struct
 */
extern unsigned long uio_refcnt(const struct uio *uio);

/**
 * @brief Increments reference count of passed uio struct
 * @param uio Pointer to uio struct to increment reference count on
 * @return Incremented reference count
 */
extern int uio_addref(struct uio *uio);

/**
 * @brief Decrements reference count of passed uio struct, calling backing endpoint's _close_ if
 * reference count is 0
 * @param uio Pointer to uio struct of backing endpoint to close
 * @return None
 */
extern void uio_close(struct uio *uio);

/**
 * @brief Calls backing endpoint's _read_
 * @param uio Pointer to uio struct of backing endpoint to read from
 * @param buf Buffer for backing endpoint to copy data into
 * @param bufsz Size of passed buffer in bytes
 * @return Number of bytes read from backing endpoint, error if backing endpoint doesn't support
 * _read_ or bufsz < 0
 */
extern long uio_read(struct uio *uio, void *buf, unsigned long bufsz);

/**
 * @brief Calls backing endpoint's _write_
 * @param uio Pointer to uio struct of backing endpoint to write to
 * @param buf Buffer for backing endpoint to copy data from
 * @param buflen Number of bytes for backing endpoint to write from buf
 * @return Number of bytes successfully written to backing endpoint, error if backing endpoint
 * doesn't support _write_ or buflen < 0
 */
extern long uio_write(struct uio *uio, const void *buf, unsigned long buflen);

/**
 * @brief Calls backing endpoint's _cntl_
 * @param uio Pointer to uio struct of backing endpoint to perform I/O cntl operation on
 * @param op Operation to perform
 * @param arg Additional argument for operation (if needed)
 * @return Output of _cntl_ if successfully called, error if backing doesn't support _cntl_
 */
extern int uio_cntl(struct uio *uio, int op, void *arg);

/**
 * @brief Creates a unidirectional pipe
 * @details Allocates memory for the pipe struct and initializes all necessary parts for the pipe
 * Allocates memory for the pipe struct and initializes all necessary parts for the pipe
 * You will have to modify the passed in pointers to point to the relevant pipe io interfaces. For
 * example, you will have to create a pipe write interface that will be referenced by the wioptr.
 * For this we also have intentionally chosen to give you freedom with how your pipes implementation
 * works internally. The following are additional details regarding implementation. You should use a
 * simple buffer of PAGE_SIZE that you can allocate using your alloc_phys_page function We recommend
 * head and tail pointers to keep track of read and write positions. This buffer is the channel in
 * which we write based on the tail and read based on the head. You will also have to use condition
 * variables to have the reader signal the writer and vice versa. This signaling has to be done on
 * updates to the buffer so that the other can properly respond. This signaling is necessary because
 * the reader and writer must sleep while they are waiting for the others actions. You should note
 * that the reader should only be forced to wait if a writer exists, and a writer should only be
 * forced to wait if a reader exists. When closing the reader or writer you should also free the
 * buffer if it is possible (consider why it would not always be possible). This implementation
 * facilitates one-way communication, allowing messages to be sent from one program to another, kind
 * of like a mailbox.
 * @param wptr Double pointer to return write uio struct to caller
 * @param rptr Double pointer to return read uio struct to caller
 * @return None
 */
extern void create_pipe(struct uio **wptr, struct uio **rptr);

// FCNTL OP CONSTANTS
//

#define FCNTL_GETEND 0  // arg is unsigned long long *
#define FCNTL_SETEND 1  // arg is unsigned long long *
#define FCNTL_GETPOS 2  // arg is unsigned long long *
#define FCNTL_SETPOS 3  // arg is unsigned long long *

#define FCNTL_MMAP 4  // arg is void **

// See also device.h for device-specific fcntl values

// The create_null_uio() function returns a pointer to a null_uio uio object,
// which supports the following operations:
//
//    close(): decrements reference count
//     read(): returns 0
//    write(): accepts all data
//     cntl(): return -ENOTSUP
//
// There is a single system-wide null uio object. Calling nulluio() increments
// its reference count and returns a pointer to it.

extern struct uio *create_null_uio(void);

// The create_memory_uio() function creates a memory-backed uio object that
// supports read, write, and control operations on a block of memory.

extern struct uio *create_memory_uio(void *buf, size_t size);

#endif  // _UIO_H_