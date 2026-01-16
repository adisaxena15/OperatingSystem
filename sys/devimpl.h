/*! @file devimpl.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Header for device implementations
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifndef _DEVIMPL_H_
#define _DEVIMPL_H_

#include "device.h"

extern int register_device(const char* name, enum device_type type, void* device_struct);

// SERIAL DEVICES
//

// The /serial_intf/ struct defines the interface to the device: the block size
// and function pointers for operations on the device. Use the functions
// /serial_open/, /serial_close/, etc. defined below to operate on the device.

/**
 * @brief Table of low-level operations of serial device. Backing endpoint of all higher level
 *serial operations. Defines the interface to the device: the block size and function pointers for
 *operations on the device.
 **/
struct serial_intf {
    /**
     * @brief mimimum block size in bytes for I/O operations
     */
    unsigned int blksz;

    /**
     * @brief Opens the device
     * @param ser Pointer to serial device instance
     */
    int (*open)(struct serial* ser);

    /**
     * @brief Closes the device
     * @param ser Pointer to serial device instance
     */
    void (*close)(struct serial* ser);

    /**
     * @brief Reads from the device
     * @param ser Pointer to serial device instance
     * @param buf buffer to read into
     * @param bufsz buffer size in bytes
     */
    int (*recv)(struct serial* ser, void* buf, unsigned int bufsz);

    /**
     * @brief Writes to the device
     * @param ser Pointer to serial device instance
     * @param buf buffer to read from
     * @param buflen buffer size in bytes to send to device
     */
    int (*send)(struct serial* ser, const void* buf, unsigned int buflen);

    /**
     * @brief Control operation on device
     * @param ser Pointer to serial device instance
     * @param op Contorl operation
     * @param arg Argument for operation
     */
    int (*cntl)(struct serial* ser, int op, void* arg);
};

// Each serial device is represented by a /serial/ struct, and all references to
// the device in the system are a pointer to this struct. The struct may be
// embedded in a larger struct, containing additional device-specific members.

/**
 * @brief Represents a serial device instance. All references to a serial device in the system are a
 *pointer to this struct. The struct may be embedded in a larger struct, containing additional
 *device-specific members.
 **/
struct serial {
    /**
     * @brief Pointer to the serial device interface
     */
    const struct serial_intf* intf;
};

/**
 * @brief Initializes the serial device interface
 * @param ser Pointer to serial device instance
 * @param intf Pointer to I/O interface to define the serial device's low level operations
 **/
static inline void serial_init(struct serial* ser, const struct serial_intf* intf) {
    ser->intf = intf;
}

// STORAGE DEVICES
//

/**
 * @brief Table of low-level operations of storage device. Backing endpoint of all higher level
 *storage operations. Defines the interface to the device: the block size and function pointers for
 *operations on the device.
 **/
struct storage_intf {
    /**
     * @brief block size in bytes
     */
    unsigned int blksz;

    /**
     * @brief Opens the storage device
     * @param sto Pointer to storage device instance
     */
    int (*open)(struct storage* sto);

    /**
     * @brief Closes the storage device
     * @param sto Pointer to storage device instance
     */
    void (*close)(struct storage* sto);

    /**
     * @brief Fetches data from the storage device
     * @param sto Pointer to storage device instance
     * @param pos Byte offset into the block device
     * @param buf Buffer to read into
     * @param bytecnt Number of bytes to fetch
     */
    long (*fetch)(struct storage* sto, unsigned long long pos, void* buf, unsigned long bytecnt);

    /**
     * @brief Stores data into the storage device
     * @param sto Pointer to storage device instance
     * @param pos Byte offset into the block device
     * @param buf Buffer to read from
     * @param bytecnt Number of bytes to store
     */
    long (*store)(struct storage* sto, unsigned long long pos, const void* buf,
                  unsigned long bytecnt);

    /**
     * @brief Control operation on device
     * @param sto Pointer to storage device instance
     * @param op Contorl operation
     * @param arg Argument for operation
     */
    int (*cntl)(struct storage* sto, int op, void* arg);
};

/**
 * @brief Represents a storage device instance. All references to a storage device in the system are
 *a pointer to this struct. The struct may be embedded in a larger struct, containing additional
 *device-specific members.
 **/
struct storage {
    /**
     * @brief Pointer to the storage device interface
     */
    const struct storage_intf* intf;

    /**
     * @brief Capacity in bytes of storage device
     */
    unsigned long long capacity;
};

/**
 * @brief Initializes the storage device interface
 * @param sto Pointer to storage device instance
 * @param intf Pointer to I/O interface to define the storage device's low level operations
 * @param cap Capacity for storage device in bytes
 **/
static inline void storage_init(struct storage* sto, const struct storage_intf* intf,
                                unsigned long long cap) {
    sto->intf = intf;
    sto->capacity = cap;
}

// VIDEO DEVICE
//

/**
 * @brief Describes video mode for frame buffer, including horizontal and vertical resolution,
 * horizontal and vertical stride, size of a pixel in bytes, and RGB position and depth.
 */
struct video_mode {
    unsigned int width, height;
    unsigned int horiz_stride;
    unsigned int vert_stride;
    unsigned char bytes_per_pixel;
    unsigned char rshift, rdepth;
    unsigned char gshift, gdepth;
    unsigned char bshift, bdepth;
};

/**
 * @brief Table of low-level operations of video device. Backing endpoint of all higher level
 *storage operations. Defines the interface to the device: the modes, mode count, and function
 *pointers for operations on the device.
 **/
struct video_intf {
    /**
     * @brief number of modes
     */
    unsigned short modecnt;

    /**
     * @brief pointer to modes
     */
    const struct video_mode* modes;

    /**
     * @brief Opens the video device
     * @param vid Pointer to video device instance
     * @param mode Mode of video device
     * @param fbufptr Double pointer to frame buffer
     */
    int (*open)(struct video* vid, int mode, void** fbufptr);

    /**
     * @brief Close the video device
     * @param vid Pointer to video device instance
     */
    void (*close)(struct video* vid);

    /**
     * @brief Flush to display with the video device
     * @param vid Pointer to video device instance
     */
    void (*flush)(struct video* vid);

    /**
     * @brief Control operation on device
     * @param vid Pointer to video device instance
     * @param op Contorl operation
     * @param arg Argument for operation
     */
    int (*cntl)(struct video* vid, int op, void* arg);
};

/**
 * @brief Represents a video device instance. All references to a video device in the system are a
 *pointer to this struct. The struct may be embedded in a larger struct, containing additional
 *device-specific members.
 **/
struct video {
    /**
     * @brief Pointer to the video device interface
     */
    const struct video_intf* intf;
};

/**
 * @brief Initializes the video device interface
 * @param vid Pointer to video device instance
 * @param intf Pointer to I/O interface to define the video device's low level operations
 **/
static inline void video_init(struct video* vid, const struct video_intf* intf) {
    vid->intf = intf;
}

#endif  // _DEVIMPL_H_