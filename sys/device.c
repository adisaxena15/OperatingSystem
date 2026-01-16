/*! @file device.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Device manager and device operations
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#include "device.h"

#include <limits.h>  // INT_MAX
#include <stddef.h>

#include "conf.h"
#include "devimpl.h"
#include "error.h"
#include "fsimpl.h"
#include "heap.h"
#include "misc.h"
#include "string.h"
#include "uio.h"
#include "uioimpl.h"

// INTERNAL MACRO DEFINITIONS
//

// INTERNAL TYPE DEFINITIONS
//

// Device list entry

struct device_record {
    struct device_record *next;
    int instno;
    enum device_type type;
    void *device_struct;
    char name[];
};

// devfs listing uio object

struct devfs_listing_uio {
    struct uio base;
    const struct device_record *dev;
};

struct serial_uio {
    struct uio base;
    struct serial *ser;
    char *buffer;
};

struct video_uio {
    struct uio base;
    struct video *vid;
    // ...
};

struct storage_uio {
    struct uio base;
    struct storage *sto;
    unsigned long pos;
    char *buffer;
};

// INTERNAL FUNCTION DECLARATIONS
//

static int devfs_open(struct filesystem *fs, const char *name, struct uio **uioptr);

static int devfs_open_listing(struct uio **uioptr);

static void devfs_listing_close(struct uio *uio);

static long devfs_listing_read(struct uio *uio, void *buf, unsigned long bufsz);

static int devfs_open_file(const char *name, struct uio **uioptr);

static int serial_open_uio(struct serial *ser, struct uio **uioptr);
static void serial_uio_close(struct uio *uio);
static long serial_uio_read(struct uio *uio, void *buf, unsigned long bufsz);
static long serial_uio_write(struct uio *uio, const void *buf, unsigned long buflen);
static int serial_uio_cntl(struct uio *uio, int op, void *arg);

static int storage_open_uio(struct storage *sto, struct uio **uioptr);
static void storage_uio_close(struct uio *uio);
static long storage_uio_read(struct uio *uio, void *buf, unsigned long bufsz);
static long storage_uio_write(struct uio *uio, const void *buf, unsigned long buflen);
static int storage_uio_cntl(struct uio *uio, int op, void *arg);

long unaligned_fetch(struct storage_uio *suio, void *buf, unsigned long bufsz);
long unaligned_store(struct storage_uio *suio, const void *buf, unsigned long buflen);

static int video_open_uio(struct video *vid, struct uio **uioptr);
static void video_uio_close(struct uio *uio);
static long video_uio_write(struct uio *uio, const void *buf, unsigned long buflen);
static int video_uio_cntl(struct uio *uio, int op, void *arg);

// EXPORTED GLOBAL VARIABLES
//

char devmgr_initialized = 0;

// INTERNAL GLOBAL VARIABLES
//

static struct device_record *devlist;

static const struct filesystem devfs = {.open = &devfs_open};

// Array of /uio_intf/ structures indexed by device type. The first entry,
// corresponding to DEV_UNDEF is for /devfs_listing_uio/.

static const struct uio_intf devfs_listing_uio_intf = {.close = &devfs_listing_close,
                                                       .read = &devfs_listing_read};

/**
 * @brief UIO interface containing read/write/close functions for serial devices.
 */
static const struct uio_intf serial_uio_intf = {.close = &serial_uio_close,
                                                .read = &serial_uio_read,
                                                .write = &serial_uio_write,
                                                .cntl = &serial_uio_cntl};

/**
 * @brief UIO interface containing read/write/cntl/close functions for storage devices.
 */
static const struct uio_intf storage_uio_intf = {.close = &storage_uio_close,
                                                 .read = &storage_uio_read,
                                                 .write = &storage_uio_write,
                                                 .cntl = &storage_uio_cntl};

/**
 * @brief UIO interface containing write/cntl/close functions for video devices.
 */
static const struct uio_intf video_uio_intf __attribute__((unused)) = {
    .close = &video_uio_close, .write = &video_uio_write, .cntl = &video_uio_cntl};

// EXPORTED FUNCTION DEFINITIONS
//

/**
 * @brief Device Manager initialization
 * @param void None
 * @return None
 */
void devmgr_init(void) {
    trace("%s()", __func__);
    devmgr_initialized = 1;
}

/**
 * @brief Function to register a device in the device list
 * @param name name of the device
 * @param type type of device
 * @param device_struct input device struct to be stored in the device list
 * @return instance number of device
 */
int register_device(const char *name, enum device_type type, void *device_struct) {
    struct device_record **dptr = &devlist;
    struct device_record *dev;
    size_t namelen;
    int instno = 0;

    assert(devmgr_initialized);
    assert(name != NULL);

    // Check that /type/ is a valid device type.

    switch (type) {
        case DEV_SERIAL:
        case DEV_STORAGE:
        case DEV_VIDEO:
            break;
        default:
            return -EINVAL;
    }

    // Walk through device list to determine the instance number for the device.
    // When we finish, /dptr/ points to the /next/ member of the last device in
    // the list (if not list is not empty) or to /devlist/ if the list is empty
    // (devlist == NULL).

    dptr = &devlist;
    while ((dev = *dptr) != NULL) {
        instno += (strcmp(name, dev->name) == 0);
        dptr = &dev->next;
    }

    // Allocate device_record struct and fill it in.

    namelen = strlen(name);
    dev = kmalloc(sizeof(*dev) + namelen + 1);
    memset(dev, 0, sizeof(*dev));

    strncpy(dev->name, name, namelen + 1);
    dev->instno = instno;
    dev->type = type;
    dev->device_struct = device_struct;

    // Insert device at end of device linked list using /dptr/.

    *dptr = dev;
    return instno;
}

/**
 * @brief Function to find a device in the device list
 * @param name name of the device to be found
 * @param type type of device to be found
 * @param instno instance number of device to be found
 * @return device struct of device if found, NULL otherwise
 */
void *find_device(const char *name, enum device_type type, int instno) {
    struct device_record *dev;

    trace("%s(%s/%s%d)", __func__, device_type_short_name(type), name, instno);

    // Find numbered instance of device in devlist

    for (dev = devlist; dev != NULL; dev = dev->next) {
        if (dev->type == type && dev->instno == instno && strcmp(name, dev->name) == 0) {
            return dev->device_struct;
        }
    }

    debug("Device %s/%s%d not found", device_type_short_name(type), name, instno);
    return NULL;
}

/**
 * @brief Function to return the short name of a device type
 * @param type type of device to find the short name for
 * @return short name of required device
 */
const char *device_type_short_name(enum device_type type) {
    switch (type) {
        case DEV_SERIAL:
            return "ser";
        case DEV_STORAGE:
            return "sto";
        case DEV_VIDEO:
            return "vid";
        default:
            return "UNK";
    }
}

/**
 * @brief Function to open the inputted serial device
 * @param ser pointer to serial device struct
 * @return 0 if device is opened, error code otherwise
 */
int serial_open(struct serial *ser) {
    if (ser == NULL) return -EINVAL;
    if (ser->intf->open != NULL)
        return ser->intf->open(ser);
    else
        return -ENOTSUP;
}

/**
 * @brief Function to close the inputted serial device
 * @param ser pointer to serial device struct
 * @return None
 */
void serial_close(struct serial *ser) {
    if (ser == NULL) return;
    if (ser->intf->close != NULL) return ser->intf->close(ser);
}

/**
 * @brief Function to call the receive function of the inputted serial device
 * @param ser pointer to serial device struct
 * @param buf buffer to read data into
 * @param bufsz size of buffer in bytes
 * @return number of bytes read, error code if unable to read
 */
int serial_recv(struct serial *ser, void *buf, unsigned int bufsz) {
    if (ser == NULL || buf == NULL) return -EINVAL;
    unsigned int const blksz = ser->intf->blksz;

    if (ser->intf->recv != NULL) {
        // bufsz may be 0, but if non-zero, must be at least blksz; round bufsz
        // to a multiple of blksz.
        if (bufsz == 0 || blksz <= bufsz)
            return ser->intf->recv(ser, buf, bufsz / blksz * blksz);
        else
            return -EINVAL;
    } else
        return -ENOTSUP;
}

/**
 * @brief Function to call the send function of the inputted serial device
 * @param ser pointer to serial device struct
 * @param buf buffer to write data from
 * @param buflen size of buffer in bytes
 * @return number of bytes written, error code if unable to write
 */
int serial_send(struct serial *ser, const void *buf, unsigned int buflen) {
    if (ser == NULL || buf == NULL) return -EINVAL;
    unsigned int const blksz = ser->intf->blksz;

    if (ser->intf->send != NULL) {
        // buflen may be 0, but if non-zero, must be at least blksz; round
        // buflen to a multiple of blksz.
        if (buflen == 0 || blksz <= buflen)
            return ser->intf->send(ser, buf, buflen / blksz * blksz);
        else
            return -EINVAL;
    } else
        return -ENOTSUP;
}

/**
 * @brief Function to call the control function of the inputted serial device
 * @param ser pointer to serial device struct
 * @param op operation of the serial device
 * @param arg arguments passed into the control oepration of the device
 * @return return value of control operation for device on success, error code on failure
 */
int serial_cntl(struct serial *ser, int op, void *arg) {
    if (ser == NULL) return -EINVAL;
    if (ser->intf->cntl != NULL)
        return ser->intf->cntl(ser, op, arg);
    else
        return -ENOTSUP;
}

/**
 * @brief Function to get the minimum block size in bytes of a serial device
 * @param ser pointer to serial device struct
 * @return block size in bytes
 */
unsigned int serial_blksz(const struct serial *ser) {
    if (ser == NULL) return -EINVAL;
    return ser->intf->blksz;
}

// STORAGE DEVICE
//

/**
 * @brief Function to open the inputted storage device
 * @param sto pointer to storage device struct
 * @return 0 if device is opened, error code otherwise
 */
int storage_open(struct storage *sto) {
    if (sto == NULL) return -EINVAL;
    if (sto->intf->open != NULL)
        return sto->intf->open(sto);
    else
        return -ENOTSUP;
}

/**
 * @brief Function to close the inputted storage device
 * @param sto pointer to storage device struct
 * @return None
 */
void storage_close(struct storage *sto) {
    if (sto == NULL) return;
    if (sto->intf->close != NULL) return sto->intf->close(sto);
}

/**
 * @brief Function to call the fetch function of the inputted storage device
 * @param sto pointer to storage device struct
 * @param pos position on storage device
 * @param buf buffer to read data into
 * @param bufsz size of buffer in bytes
 * @return number of bytes read, error code if error
 */
long storage_fetch(struct storage *sto, unsigned long long pos, void *buf, unsigned long bufsz) {
    if (sto == NULL || buf == NULL) return -EINVAL;
    unsigned int const blksz = sto->intf->blksz;

    if (sto->intf->fetch != NULL) {
        // bufsz may be 0, but if non-zero, must be at least blksz; round
        // bufsz to a multiple of blksz.
        if ((bufsz == 0 || blksz <= bufsz) && (pos % blksz == 0))
            return sto->intf->fetch(sto, pos, buf, bufsz);
        else
            return -EINVAL;
    } else
        return -ENOTSUP;
}

/**
 * @brief Function to call the store function of the inputted storage device
 * @param sto pointer to storage device struct
 * @param pos position on storage device
 * @param buf buffer to write data from
 * @param buflen size of buffer in bytes
 * @return number of bytes written, error code if error
 */
long storage_store(struct storage *sto, unsigned long long pos, const void *buf,
                   unsigned long buflen) {
    if (sto == NULL || buf == NULL) return -EINVAL;
    unsigned int const blksz = sto->intf->blksz;

    if (sto->intf->store != NULL) {
        // buflen may be 0, but if non-zero, must be at least blksz; round
        // buflen to a multiple of blksz.
        if ((buflen == 0 || blksz <= buflen) && (pos % blksz == 0))
            return sto->intf->store(sto, pos, buf, buflen);
        else
            return -EINVAL;
    } else
        return -ENOTSUP;
}

/**
 * @brief Function to call the control function of the inputted storage device
 * @param sto pointer to storage device struct
 * @param op operation of the storage device
 * @param arg arguments passed into the control oepration of the device
 * @return return value of control operation for device on success, error code on failure
 */
int storage_cntl(struct storage *sto, int op, void *arg) {
    if (sto == NULL) return -EINVAL;
    if (sto->intf->cntl != NULL)
        return sto->intf->cntl(sto, op, arg);
    else
        return -ENOTSUP;
}

/**
 * @brief Function to get the block size in bytes of a storage device
 * @param sto pointer to storage device struct
 * @return block size in bytes
 */
unsigned int storage_blksz(const struct storage *sto) {
    if (sto == NULL) return -EINVAL;
    return sto->intf->blksz;
}

/**
 * @brief Function to get the storage capacity in bytes of a storage device
 * @param sto pointer to storage device struct
 * @return capacity
 */
unsigned long long storage_capacity(const struct storage *sto) {
    if (sto == NULL) return -EINVAL;
    return sto->capacity;
}

// VIDEO DEVICE
//

int video_open(struct video *vid, int mode, void **fbptr) {
    if (vid->intf->open != NULL)
        return vid->intf->open(vid, mode, fbptr);
    else
        return -ENOTSUP;
}

void video_close(struct video *vid) {
    if (vid->intf->close != NULL) return vid->intf->close(vid);
}

void video_flush(struct video *vid) {
    if (vid->intf->flush != NULL) return vid->intf->flush(vid);
}

int video_cntl(struct video *vid, int op, void *arg) {
    if (vid->intf->cntl != NULL)
        return vid->intf->cntl(vid, op, arg);
    else
        return -ENOTSUP;
}

/**
 * @brief Mounts the device filesystem at the specified mount point name
 * @param name mount point name
 * @return 0 if successful, negative error code if error
 */
int mount_devfs(const char *name) { return attach_filesystem(name, (struct filesystem *)&devfs); }

// INTERNAL FUNCTION DEFINITIONS
//

/**
 * @brief Opens the device file system for the listing or the device
 * @param fs filesystem
 * @param name name of file to be opened in case of file, or NULL for listing
 * @param uioptr double pointer for uio struct
 * @return 0 if successful, negative error code if error
 */
int devfs_open(struct filesystem *fs, const char *name, struct uio **uioptr) {
    if (name == NULL || *name == '\0')
        return devfs_open_listing(uioptr);
    else
        return devfs_open_file(name, uioptr);
}

/**
 * @brief Opens the device file system for listing
 * @param uioptr double pointer for uio struct
 * @return 0 on success
 */
int devfs_open_listing(struct uio **uioptr) {
    struct devfs_listing_uio *ls;

    ls = kcalloc(1, sizeof(*ls));
    ls->dev = devlist;

    // Note: The listing uio_intf is at index DEV_UNDEF in /devfs_uio_intfs/.
    *uioptr = uio_init1(&ls->base, &devfs_listing_uio_intf);

    return 0;
}

/**
 * @brief Closes a device listing uio object
 * @param uio pointer to uio object to be closed
 */
void devfs_listing_close(struct uio *uio) {
    struct devfs_listing_uio *const ls = (struct devfs_listing_uio *)uio;
    kfree(ls);
}

/**
 * @brief Reads the next device name into the buffer
 * @param uio pointer to device listing uio object
 * @param buf buffer to read the device name into
 * @param bufsz size of the buffer
 * @return number of bytes read, 0 if no more devices,
 */
long devfs_listing_read(struct uio *uio, void *buf, unsigned long bufsz) {
    struct devfs_listing_uio *const ls = (struct devfs_listing_uio *)uio;
    size_t len;

    if (ls->dev != NULL) {
        len = snprintf(buf, bufsz, "%s%d", ls->dev->name, ls->dev->instno);
        ls->dev = ls->dev->next;
        return (len < bufsz) ? len : bufsz;
    } else
        return 0;
}

/**
 * @brief Opens a device and wraps it in a uio object
 * @param name device name (NULL or empty string for listing all devices)
 * @param uioptr pointer to uio struct pointer to be filled in
 * @return 0 if successful, negative error code if error
 */
int devfs_open_file(const char *name, struct uio **uioptr) {
    const char *dp = NULL;  // position of trailing sequence of digits
    const char *s;          // position in string
    struct device_record *dev;
    unsigned long instno;

    // Parse name as device name and instance number

    for (s = name; *s != '\0'; s++) {
        if ('0' <= *s && *s <= '9') {
            if (dp == NULL) dp = s;
        } else
            dp = NULL;
    }

    if (dp == NULL) return -ENOENT;

    instno = strtoul(dp, NULL, 10);

    // Find the device record for the device.

    for (dev = devlist; dev != NULL; dev = dev->next) {
        if (strncmp(name, dev->name, dp - name) == 0 && dev->name[dp - name] == '\0' &&
            dev->instno == instno) {
            switch (dev->type) {
                case DEV_SERIAL:
                    return serial_open_uio(dev->device_struct, uioptr);
                case DEV_STORAGE:
                    return storage_open_uio(dev->device_struct, uioptr);
                case DEV_VIDEO:
                    return video_open_uio(dev->device_struct, uioptr);
                default:
                    panic("Bad device type");
            }
        }
    }

    return -ENOENT;
}

/**
 * @brief Opens a serial device and wraps it in a uio object
 * @param ser pointer to serial device struct
 * @param uioptr pointer to uio struct pointer to be populated
 * @return 0 if successful, negative error code if error
 */
int serial_open_uio(struct serial *ser, struct uio **uioptr) {
    struct serial_uio *suio;
    int result;

    // Try to open device

    result = serial_open(ser);

    if (result != 0) return result;

    suio = kcalloc(1, sizeof(*suio));

    // we also need to create an internal buffer
    // to deal with unaligned reads
    suio->buffer = kcalloc(1, ser->intf->blksz);

    suio->ser = ser;
    *uioptr = uio_init1(&suio->base, &serial_uio_intf);
    return 0;
}

/**
 * @brief Closes a serial uio object and the underlying serial device
 * @param uio pointer to uio object to be closed
 * @return None
 */
void serial_uio_close(struct uio *uio) {
    struct serial_uio *suio = (struct serial_uio *)uio;

    serial_close(suio->ser);
    kfree(suio->buffer);
    kfree(suio);
}

/**
 * @brief Reads data from a serial device into a buffer
 * @param uio pointer to serial uio object
 * @param buf pointer to buffer to read data into
 * @param bufsz size of buffer in bytes
 * @return number of bytes read, negative error code if error
 */
long serial_uio_read(struct uio *uio, void *buf, unsigned long bufsz) {
    struct serial_uio *suio = (struct serial_uio *)uio;
    unsigned int blksz = suio->ser->intf->blksz;
    unsigned long aligned_bufsz = ROUND_DOWN(bufsz, blksz);
    int result = serial_recv(suio->ser, buf, aligned_bufsz);

    if (result < 0) return result;

    if (bufsz % blksz != 0 && aligned_bufsz == result) {
        // the device has filled the buffer as much as it can,
        // so we must use our internal buffer to fill the rest.
        result = serial_recv(suio->ser, suio->buffer, blksz);
        if (result <= 0) return aligned_bufsz;
        memcpy(buf + aligned_bufsz, suio->buffer, bufsz % blksz);
        return bufsz;
    }

    return result;
}

/**
 * @brief Writes data from a buffer to a serial device
 * @param uio pointer to serial uio object
 * @param buf pointer to buffer containing data to write
 * @param buflen size of buffer in bytes
 * @return number of bytes written, negative error code if error
 */
long serial_uio_write(struct uio *uio, const void *buf, unsigned long buflen) {
    struct serial_uio *suio = (struct serial_uio *)uio;
    return serial_send(suio->ser, buf, buflen);
}

/**
 * @brief Performs control operation on a serial device
 * @param uio pointer to serial uio object
 * @param op control operation of serial device
 * @param arg arguments of control operation
 * @return output of control operation on success, negative error code if error
 */
int serial_uio_cntl(struct uio *uio, int op, void *arg) {
    struct serial_uio *suio = (struct serial_uio *)uio;
    return serial_cntl(suio->ser, op, arg);
}

/**
 * @brief Opens a storage device and wraps it in a uio object
 * @param sto pointer to storage device struct
 * @param uioptr pointer to uio struct pointer to be filled in
 * @return 0 if successful, negative error code if error
 */
int storage_open_uio(struct storage *sto, struct uio **uioptr) {
    struct storage_uio *suio;
    int result;

    // Try to open device

    result = storage_open(sto);

    if (result != 0) return result;

    suio = kcalloc(1, sizeof(*suio));

    // we also need to create an internal buffer
    // to deal with unaligned reads and writes
    suio->buffer = kcalloc(1, sto->intf->blksz);

    suio->sto = sto;
    suio->pos = 0;
    *uioptr = uio_init1(&suio->base, &storage_uio_intf);
    return 0;
}

/**
 * @brief Closes a storage uio object and the underlying storage device
 * @param uio pointer to uio object to be closed
 */
void storage_uio_close(struct uio *uio) {
    struct storage_uio *suio = (struct storage_uio *)uio;
    storage_close(suio->sto);

    kfree(suio->buffer);
    kfree(suio);
}

/**
 * @brief Reads data from a storage device into a buffer and updates internal pos
 * @param uio pointer to storage uio object
 * @param buf pointer to buffer to read data into
 * @param bufsz size of buffer in bytes
 * @return number of bytes read, negative error code if error
 */
long storage_uio_read(struct uio *uio, void *buf, unsigned long bufsz) {
    struct storage_uio *suio = (struct storage_uio *)uio;
    long bytes_read = 0;
    long result;
    unsigned int blksz = suio->sto->intf->blksz;

    if (suio->pos % blksz != 0) {  // unaligned starting pos
        bytes_read = unaligned_fetch(suio, buf, bufsz);
        if (bytes_read <= 0) return bytes_read;
        suio->pos += bytes_read;
        bufsz -= bytes_read;
    }

    if (bufsz == 0) return bytes_read;

    result = storage_fetch(suio->sto, suio->pos, buf + bytes_read, ROUND_DOWN(bufsz, blksz));

    if (result < 0) return (bytes_read > 0) ? bytes_read : result;

    suio->pos += result;

    if (result < ROUND_DOWN(bufsz, blksz)) return bytes_read + result;

    bytes_read += result;
    bufsz -= result;

    if (bufsz % blksz != 0) {  // unaligned bufsz
        result = unaligned_fetch(suio, buf + bytes_read, bufsz);
        if (result < 0) return bytes_read;
        bytes_read += result;
        suio->pos += result;
    }

    return bytes_read;
}

/**
 * @brief Helper function for `storage_uio_read`.
 * Aligns reads for when pos or bufsz is unaligned.
 * @param suio pointer to storage_uio object
 * @param buf pointer to buffer to read data into
 * @param bufsz size of buffer in bytes
 * @return number of bytes read, negative error code if error
 */
long unaligned_fetch(struct storage_uio *suio, void *buf, unsigned long bufsz) {
    unsigned int blksz = suio->sto->intf->blksz;
    long bytes_read = storage_fetch(suio->sto, ROUND_DOWN(suio->pos, blksz), suio->buffer, blksz);

    if (bytes_read < 0) return bytes_read;

    bytes_read = MIN(blksz - suio->pos % blksz, bufsz);
    memcpy(buf, suio->buffer + suio->pos % blksz, bytes_read);
    return bytes_read;
}

/**
 * @brief Writes data from a buffer to a storage device
 * @param uio pointer to storage uio object
 * @param buf pointer to buffer containing data to write
 * @param buflen size of buffer in bytes
 * @return number of bytes written, negative error code if error
 */
long storage_uio_write(struct uio *uio, const void *buf, unsigned long buflen) {
    struct storage_uio *suio = (struct storage_uio *)uio;
    long bytes_written = 0;
    long result;
    unsigned int blksz = suio->sto->intf->blksz;

    if (suio->pos % blksz != 0) {  // unaligned starting pos
        result = unaligned_store(suio, buf, buflen);
        if (result < 0) return result;
        bytes_written = result;
        suio->pos += bytes_written;
        buflen -= bytes_written;
    }

    if (buflen == 0) return bytes_written;

    result = storage_store(suio->sto, suio->pos, buf + bytes_written, ROUND_DOWN(buflen, blksz));

    if (result < 0) return (bytes_written > 0) ? bytes_written : result;

    suio->pos += result;

    if (result < ROUND_DOWN(buflen, blksz)) return bytes_written + result;

    bytes_written += result;
    buflen -= result;

    if (buflen % blksz != 0) {  // unaligned buflen
        result = unaligned_store(suio, buf + bytes_written, buflen);
        if (result < 0) return bytes_written;
        bytes_written += result;
        suio->pos += result;
    }

    return bytes_written;
}

/**
 * @brief Helper function for `storage_uio_write`.
 * Aligns writes for when pos or bufsz is unaligned.
 * @param suio pointer to storage_uio object
 * @param buf pointer to buffer to read data into
 * @param buflen size of buffer in bytes
 * @return number of bytes read, negative error code if error
 */
long unaligned_store(struct storage_uio *suio, const void *buf, unsigned long buflen) {
    unsigned int blksz = suio->sto->intf->blksz;
    long bytes_written = 0;
    // fetch the original block of data
    long result = storage_fetch(suio->sto, ROUND_DOWN(suio->pos, blksz), suio->buffer, blksz);

    if (result < 0) return result;

    bytes_written = MIN(blksz - suio->pos % blksz, buflen);
    // overwrite with new data and store in its place
    memcpy(suio->buffer + suio->pos % blksz, buf, bytes_written);
    result = storage_store(suio->sto, ROUND_DOWN(suio->pos, blksz), suio->buffer, blksz);

    if (result < 0) return result;

    return bytes_written;
}

/**
 * @brief Performs a control operation on a storage device
 * @param uio pointer to storage uio object
 * @param op control operation code
 * @param arg pointer to argument for control operation
 * @return 0 if successful, negative error code if error
 */
int storage_uio_cntl(struct uio *uio, int op, void *arg) {
    struct storage_uio *suio = (struct storage_uio *)uio;
    if (op == FCNTL_SETPOS) {
        size_t *pos = (size_t *)arg;
        if (pos == NULL || *pos > suio->sto->capacity) return -EINVAL;
        suio->pos = *pos;
        return 0;
    }
    if (op == FCNTL_GETPOS) {
        size_t *pos = (size_t *)arg;
        if (pos == NULL) return -EINVAL;
        *pos = suio->pos;
        return 0;
    }
    return storage_cntl(suio->sto, op, arg);
}

int video_open_uio(struct video *vid, struct uio **uioptr) { return -ENOTSUP; }

void video_uio_close(struct uio *uio) {}

long video_uio_write(struct uio *uio, const void *buf, unsigned long buflen) { return -ENOTSUP; }

int video_uio_cntl(struct uio *uio, int op, void *arg) { return -ENOTSUP; }
