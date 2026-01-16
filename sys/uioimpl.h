/*! @file uioimpl.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Header for uio implementations
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifndef _UIOIMPL_H_
#define _UIOIMPL_H_

struct uio;  // forward decl.

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
    void (*close)(struct uio* uio);

    /**
     * @brief Reads from sequential access I/O endpoint
     * @param uio A sequential access I/O endpoint
     * @param buf buffer read into
     * @param bufsz buffer size in bytes
     */
    long (*read)(struct uio* uio, void* buf, unsigned long bufsz);

    /**
     * @brief Writes to sequential access I/O endpoint
     * @param uio A sequential access I/O endpoint
     * @param buf Buffer to read from
     * @param buflen Number of bytes to write
     */
    long (*write)(struct uio* uio, const void* buf, unsigned long buflen);

    /**
     * @brief Control operation on I/O endpoint
     * @param uio An I/O endpoint to control/interrogate
     * @param op Operation (FCNTL_* defined in uio.h)
     * @param arg Argument for operation
     */
    int (*cntl)(struct uio* uio, int op, void* arg);
};

/**
 * @brief User Input/Output abstraction for devices, files, pipes, etc.
 */
struct uio {
    const struct uio_intf* intf;  ///< I/O interface for backing endpoint
    unsigned long refcnt;         ///< Number of active references to this I/O endpoint
};

// Initialize a uio object with a reference count of zero.

static inline struct uio* uio_init0(struct uio* uio, const struct uio_intf* intf) {
    uio->intf = intf;
    uio->refcnt = 0;
    return uio;
}

// Initialize a uio object with a reference count of one.

static inline struct uio* uio_init1(struct uio* uio, const struct uio_intf* intf) {
    uio->intf = intf;
    uio->refcnt = 1;
    return uio;
}

#endif