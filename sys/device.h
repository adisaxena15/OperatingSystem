/*! @file device.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Interface to device system
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifndef _DEVICE_H_
#define _DEVICE_H_

enum device_type {
    DEV_UNDEF,
    DEV_SERIAL,   // UART, RTC, viorng, viohi
    DEV_STORAGE,  // vioblk
    DEV_VIDEO     // viogpu
};

struct serial;   // opaque decl.
struct storage;  // opaque decl.
struct video;    // opaque decl.

extern char devmgr_initialized;

// EXPORTED FUNCTION DECLARATIONS
//

extern void devmgr_init(void);
extern int mount_devfs(const char* name);
extern void* find_device(const char* name, enum device_type type, int instno);

static inline struct serial* find_serial(const char* name, int instno) {
    return find_device(name, DEV_SERIAL, instno);
}

static inline struct storage* find_storage(const char* name, int instno) {
    return find_device(name, DEV_STORAGE, instno);
}

static inline struct video* find_video(const char* name, int instno) {
    return find_device(name, DEV_VIDEO, instno);
}

extern const char* device_type_short_name(enum device_type type);

// Class-specific functions
//

extern int serial_open(struct serial* ser);

extern void serial_close(struct serial* ser);

extern int serial_recv(struct serial* ser, void* buf, unsigned int bufsz);

extern int serial_send(struct serial* ser, const void* buf, unsigned int buflen);

extern int serial_cntl(struct serial* ser, int op, void* arg);

extern unsigned int serial_blksz(const struct serial* ser);

extern int storage_open(struct storage* sto);

extern void storage_close(struct storage* sto);

extern long storage_fetch(struct storage* sto, unsigned long long pos, void* buf,
                          unsigned long bytecnt);

extern long storage_store(struct storage* sto, unsigned long long pos, const void* buf,
                          unsigned long bytecnt);

extern int storage_cntl(struct storage* sto, int op, void* arg);

extern unsigned int storage_blksz(const struct storage* sto);

extern unsigned long long storage_capacity(const struct storage* sto);

extern int video_open(struct video* vid, int mode, void** fbptr);
extern void video_close(struct video* vid);
extern void video_flush(struct video* vid);
extern int video_cntl(struct video* vid, int op, void* arg);

#endif