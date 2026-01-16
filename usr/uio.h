// uio.h - Abstract uio interface
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

/*! @file uio.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Unified Uniform I/O object
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA
*/

#ifndef _IO_H_
#define _IO_H_

#include "error.h"
#include "string.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief User Input/Output abstraction for devices, files, pipes, etc.
 */
struct uio {
    const struct uio_intf * intf; ///< I/O interface for backing endpoint 
    unsigned long refcnt; ///< Number of active references to this I/O endpoint 
};

/**
 * @brief Table of RWOC-type functions that backing endpoints may support. These functions allow
 * modification of a specific device, file, pipe, or backing endpoint via the I/O abstraction.
 * Backing endpoints may support any combination of the provided operations. Return values for 
 * each function depend on backing endpoint.
 */
struct uio_intf {
    /**
     * @brief Closes I/O endpoint
     * @param uio Pointer to I/O endpoint
     */
    void (*close)(struct uio * uio);

    /**
     * @brief Reads from sequential access I/O endpoint
     * @param uio A sequential access I/O endpoint
     * @param buf buffer read into
     * @param bufsz buffer size in bytes
     */
    long (*read)(struct uio * uio, void * buf, unsigned long bufsz);

    /**
     * @brief Writes to sequential access I/O endpoint
     * @param uio A sequential access I/O endpoint
     * @param buf Buffer to read from
     * @param buflen Number of bytes to write
     */
    long (*write)(struct uio * uio, const void * buf, unsigned long buflen);

    /**
     * @brief Control operation on I/O endpoint
     * @param uio An I/O endpoint to control/interrogate
     * @param op Operation (FCNTL_* defined in uio.h)
     * @param arg Argument for operation
     */
    int (*cntl)(struct uio * uio, int op, void * arg);
};

/**
 * @brief An uioterm object is a wrapper around a "raw" Uniform I/O object. It provides newline conversuion and interactive line-editing for string input.
 */
struct uioterm {
    struct uio uio; ///<Uniform I/O abstraction
    struct uio * rawuio; ///< "raw" Uniform I/O object
    int8_t cr_out; ///< Output CRLF normalization
    int8_t cr_in; ///< Input CRLF normalization
};

#define FCNTL_GETEND 0 // arg is unsigned long long *
#define FCNTL_SETEND 1 // arg is unsigned long long *
#define FCNTL_GETPOS 2 // arg is unsigned long long *
#define FCNTL_SETPOS 3 // arg is unsigned long long *

#define FCNTL_MMAP   4 // arg is void **

// refcount functions
/**
 * @brief Returns reference count of passed uio struct
 * @param uio Pointer to uio struct to check reference count on
 * @return Reference count of passed uio struct
 */
extern unsigned long uio_refcnt(const struct uio * uio);

/**
 * @brief Increments reference count of passed uio struct
 * @param uio Pointer to uio struct to increment reference count on
 * @return Incremented reference count
 */
extern int uio_addref(struct uio * uio);

/**
 * @brief Decrements reference count of passed uio struct, calling backing endpoint's _close_ if reference count is 0
 * @param uio Pointer to uio struct of backing endpoint to close
 * @return None
 */
extern void uio_close(struct uio * uio);

/**
 * @brief Calls backing endpoint's _read_
 * @param uio Pointer to uio struct of backing endpoint to read from
 * @param buf Buffer for backing endpoint to copy data into
 * @param bufsz Size of passed buffer in bytes
 * @return Number of bytes read from backing endpoint, error if backing endpoint doesn't support _read_ or bufsz < 0
 */
extern long uio_read(struct uio * uio, void * buf, unsigned long bufsz);

/**
 * @brief Calls backing endpoint's _write_
 * @param uio Pointer to uio struct of backing endpoint to write to
 * @param buf Buffer for backing endpoint to copy data from
 * @param buflen Number of bytes for backing endpoint to write from buf
 * @return Number of bytes successfully written to backing endpoint, error if backing endpoint doesn't support _write_ or buflen < 0
 */
extern long uio_write(struct uio * uio, const void * buf, unsigned long buflen);

/**
 * @brief Calls backing endpoint's _cntl_
 * @param uio Pointer to uio struct of backing endpoint to perform I/O cntl operation on
 * @param op Operation to perform
 * @param arg Additional argument for operation (if needed)
 * @return Output of _cntl_ if successfully called, error if backing doesn't support _cntl_
 */
extern int uio_cntl(struct uio * uio, int op, void * arg);

// put, get, print
/**
 * @brief Writes a character to an Uniform I/O abstraction
 * @param uio The Uniform I/O abstraction to interact with
 * @param c The character to write
 * @return The character writen, or negative error code
 */
static inline int uio_putc(struct uio * uio, char c);

/**
 * @brief Reads a character to an Uniform I/O abstraction
 * @param uio The Uniform I/O abstraction to interact with
 * @return The character read, or negative error code
 */
static inline int uio_getc(struct uio * uio);

/**
 * @brief Writes a string of characters to an Uniform I/O abstraction
 * @param uio The Uniform I/O abstraction to interact with
 * @param s A pointer to the string to be written
 * @return The number of character's written, or negative error code
 */
int uio_puts(struct uio * uio, const char * s);

/**
 * @brief This function can take a variable amount of arguments. It formats and prints a string to an Uniform I/O abstraction
 * @param uio The Uniform I/O abstraction to interact with
 * @param fmt A format specifier that specifies formatting and printing guidelines
 * @return The number of character's written, or negative error code
 */
long uio_printf(struct uio * uio, const char * fmt, ...);

/**
 * @brief Helper for uioprintf that calls vgprintf which writes the properly formated string.
 * @param uio The Uniform I/O abstraction to interact with
 * @param fmt A format specifier that specifies formatting and printing guidelines
 * @param ap Optional additional paramaters for formating
 * @return The number of character's written, or negative error code
 */
long uio_vprintf(struct uio * uio, const char * fmt, va_list ap);

// Uniform I/O term provides three features:
//
//     1. Input CRLF normalization. Any of the following character sequences in
//        the input are converted into a single \n:
//
//            (a) \r\n,
//            (b) \r not followed by \n,
//            (c) \n not preceeded by \r.
//
//     2. Output CRLF normalization. Any \n not preceeded by \r, or \r not
//        followed by \n, is written as \r\n. Sequence \r\n is written as \r\n.
//
//     3. Line editing. The uioterm_getsn function provides line editing of the
//        input.
//
// Input CRLF normalization works by maintaining one bit of state: cr_in.
// Initially cr_in = 0. When a character ch is read from rawuio:
// 
// if cr_in = 0 and ch == '\r': return '\n', cr_in <- 1;
// if cr_in = 0 and ch != '\r': return ch;
// if cr_in = 1 and ch == '\r': return \n;
// if cr_in = 1 and ch == '\n': skip, cr_in <- 0;
// if cr_in = 1 and ch != '\r' and ch != '\n': return ch, cr_in <- 0.
//
// Ouput CRLF normalization works by maintaining one bit of state: cr_out.
// Initially, cr_out = 0. When a character ch is written to Uniform I/O term:
//
// if cr_out = 0 and ch == '\r': output \r\n to rawuio, cr_out <- 1;
// if cr_out = 0 and ch == '\n': output \r\n to rawuio;
// if cr_out = 0 and ch != '\r' and ch != '\n': output ch to rawuio;
// if cr_out = 1 and ch == '\r': output \r\n to rawuio;
// if cr_out = 1 and ch == '\n': no ouput, cr_out <- 0;
// if cr_out = 1 and ch != '\r' and ch != '\n': output ch, cr_out <- 0.


// An uioterm object is a wrapper around a "raw" Uniform I/O object. It provides newline
// conversuion and interactive line-editing for string input.
//
// uioterm_init initializes an uioterm object for use with an underlying raw Uniform I/O
// object. The /uiot/ argument is a pointer to an uioterm struct to initialize
// and /rawuio/ is a pointer to an uio_intf that provides backing Uniform I/O.
/**
 * @brief Initializes an uioterm object for use with an underlying raw Uniform I/O object.
 * @param uiot A pointer to an uioterm struct to initialize
 * @param rawuio A pointer to an uio_intf that provides backing Uniform I/O
 * @return A pointer to the uioterm's Uniform I/O abstraction
 */
struct uio * uioterm_init(struct uioterm * uiot, struct uio * rawuio);


// The uioterm_getsn function reads a line of input of up to /n/ characters from
// a terminal Uniform I/O object. The function supports line editing (delete and
// backspace) and limits the input to /n/ characters.
/**
 * @brief The uioterm_getsn function reads a line of input of up to /n/ characters from a terminal Uniform I/O object. The function supports line editing (delete and backspace) and limits the input to /n/ characters.
 * @param uiot A pointer to an uioterm struct
 * @param buf A pointer to the buffer that will be populated with read input
 * @param n Maximum number of possible character's to be inputted
 * @return Null
 * @
 */
char * uioterm_getsn(struct uioterm * uiot, char * buf, size_t n);

// definitions for putc and getc
static inline int uio_putc(struct uio * uio, char c) {
    long wlen;

    wlen = uio_write(uio, &c, 1);

    if (wlen < 0)
        return wlen;
    else if (wlen == 0)
        return -EIO;
    else
        return (unsigned char)c;
}

static inline int uio_getc(struct uio * uio) {
    long rlen;
    char c;

    rlen = uio_read(uio, &c, 1);
    
    if (rlen < 0)
        return rlen;
    else if (rlen == 0)
        return -EIO;
    else
        return (unsigned char)c;
}

#endif // _IO_H_