
/*! @file uio.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Uniform I/O interface
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifdef UIO_DEBUG
#define DEBUG
#endif

#ifdef UIO_TRACE
#define TRACE
#endif

#include "uio.h"

#include <stddef.h>  // for NULL and offsetof

#include "error.h"
#include "heap.h"
#include "memory.h"
#include "misc.h"
#include "string.h"
#include "thread.h"
#include "uioimpl.h"

static void nulluio_close(struct uio* uio);

static long nulluio_read(struct uio* uio, void* buf, unsigned long bufsz);

static long nulluio_write(struct uio* uio, const void* buf, unsigned long buflen);

struct pipe {
    struct uio wio;             
    struct uio rio;             
    char* buffer;               
    unsigned long head;      
    unsigned long tail;          
    unsigned long size;          
    struct condition data_avail; 
    struct condition space_avail;
    struct lock pipe_lock;      
};
static void pipe_write_close(struct uio* uio);
static long pipe_write(struct uio* uio, const void* buf, unsigned long buflen);
static void pipe_read_close(struct uio* uio);
static long pipe_read(struct uio* uio, void* buf, unsigned long bufsz);
static const struct uio_intf pipe_write_intf = {
    .close = pipe_write_close,
    .write = pipe_write
};
static const struct uio_intf pipe_read_intf = {
    .close = pipe_read_close,
    .read = pipe_read
};

// INTERNAL GLOBAL VARIABLES AND CONSTANTS
//

void uio_close(struct uio* uio) {
    debug("uio_close: refcnt=%d, has_close=%d", uio->refcnt, (uio->intf->close != NULL));

    // Decrement reference count if it's greater than 0
    if (uio->refcnt > 0) {
        uio->refcnt--;
        debug("uio_close: decremented refcnt to %d", uio->refcnt);
    }

    // Only call the actual close method when refcnt reaches 0
    if (uio->refcnt == 0 && uio->intf->close != NULL) {
        debug("uio_close: calling close method");
        uio->intf->close(uio);
    } else if (uio->refcnt > 0) {
        debug("uio_close: NOT calling close (refcnt=%d still has references)", uio->refcnt);
    }
}

long uio_read(struct uio* uio, void* buf, unsigned long bufsz) {
    if (uio->intf->read != NULL) {
        if (0 <= (long)bufsz)
            return uio->intf->read(uio, buf, bufsz);
        else
            return -EINVAL;
    } else
        return -ENOTSUP;
}

long uio_write(struct uio* uio, const void* buf, unsigned long buflen) {
    if (uio->intf->write != NULL) {
        if (0 <= (long)buflen)
            return uio->intf->write(uio, buf, buflen);
        else
            return -EINVAL;
    } else
        return -ENOTSUP;
}

int uio_cntl(struct uio* uio, int op, void* arg) {
    if (uio->intf->cntl != NULL)
        return uio->intf->cntl(uio, op, arg);
    else
        return -ENOTSUP;
}

unsigned long uio_refcnt(const struct uio* uio) {
    assert(uio != NULL);
    return uio->refcnt;
}

int uio_addref(struct uio* uio) { return ++uio->refcnt; }

struct uio* create_null_uio(void) {
    static const struct uio_intf nulluio_intf = {
        .close = &nulluio_close, .read = &nulluio_read, .write = &nulluio_write};

    static struct uio nulluio = {.intf = &nulluio_intf, .refcnt = 0};

    return &nulluio;
}

static void nulluio_close(struct uio* uio) {
    // ...
}

static long nulluio_read(struct uio* uio, void* buf, unsigned long bufsz) {
    // ...
    return -ENOTSUP;
}

static long nulluio_write(struct uio* uio, const void* buf, unsigned long buflen) {
    // ...
    return -ENOTSUP;
}

void create_pipe(struct uio** wptr, struct uio** rptr) {
    struct pipe* p = kmalloc(sizeof(struct pipe));
    if (p == NULL) {
        *wptr = NULL;
        *rptr = NULL;
        return;
    }
    p->buffer = alloc_phys_page();
    if (p->buffer == NULL) {
        kfree(p);
        *wptr = NULL;
        *rptr = NULL;
        return;
    }
    p->head = 0;
    p->tail = 0;
    p->size = PAGE_SIZE;
    //we need to initialize the condition variables and lock
    condition_init(&p->data_avail, "pipe_data");
    condition_init(&p->space_avail, "pipe_space");
    lock_init(&p->pipe_lock);
    uio_init1(&p->wio, &pipe_write_intf);
    uio_init1(&p->rio, &pipe_read_intf);
    //we need to initialize the reference counts to 0
    p->wio.refcnt = 0;
    p->rio.refcnt = 0;
    *wptr = &p->wio;
    *rptr = &p->rio;
}

static long pipe_write(struct uio* uio, const void* buf, unsigned long buflen) {
    struct pipe* p = (struct pipe*)((char*)uio - offsetof(struct pipe, wio));
    unsigned long written = 0;
    if (buf == NULL || buflen == 0) {
        return 0;
    }
    lock_acquire(&p->pipe_lock);
    while (written < buflen) {
        //we need to check if there are no readers (broken pipe)
        if (p->rio.refcnt == 0) {
            lock_release(&p->pipe_lock);
            return (written > 0) ? (long)written : -EPIPE;
        }
        //we need to free space in ring buffer (leave 1 byte empty)
        unsigned long space;
        if (p->tail >= p->head) {
            space = p->size - (p->tail - p->head) - 1;
        } else {
            space = p->head - p->tail - 1;
        }
        //we need to wait if buffer is full but readers still exist
        while (space == 0 && p->rio.refcnt > 0) {
            condition_wait(&p->space_avail);
            if (p->tail >= p->head) {
                space = p->size - (p->tail - p->head) - 1;
            } else {
                space = p->head - p->tail - 1;
            }
            if (p->rio.refcnt == 0) {
                lock_release(&p->pipe_lock);
                return (written > 0) ? (long)written : -EPIPE;
            }
        }
        if (space == 0 && p->rio.refcnt == 0) {
            lock_release(&p->pipe_lock);
            return (written > 0) ? (long)written : -EPIPE;
        }
        unsigned long to_write = buflen - written;
        if (to_write > space) {
            to_write = space;
        }
        unsigned long tail_to_end = p->size - p->tail;
        if (to_write <= tail_to_end) {
            memcpy(p->buffer + p->tail, (const char*)buf + written, to_write);
            p->tail = (p->tail + to_write) % p->size;
        } else {
            memcpy(p->buffer + p->tail, (const char*)buf + written, tail_to_end);
            memcpy(p->buffer,
                   (const char*)buf + written + tail_to_end,
                   to_write - tail_to_end);
            p->tail = to_write - tail_to_end;
        }
        written += to_write;
        condition_broadcast(&p->data_avail);
    }
    lock_release(&p->pipe_lock);
    return (long)written;
}

static long pipe_read(struct uio* uio, void* buf, unsigned long bufsz) {
    struct pipe* p = (struct pipe*)((char*)uio - offsetof(struct pipe, rio));
    if (buf == NULL || bufsz == 0) {
        return 0;
    }
    lock_acquire(&p->pipe_lock);
    //we need to calculate the available data in the ring buffer
    unsigned long data;
    if (p->tail >= p->head) {
        data = p->tail - p->head;
    } else {
        data = p->size - (p->head - p->tail);
    }
    //we need to wait if buffer is empty but writers still exist
    while (data == 0 && p->wio.refcnt > 0) {
        condition_wait(&p->data_avail);
        if (p->tail >= p->head) {
            data = p->tail - p->head;
        } else {
            data = p->size - (p->head - p->tail);
        }
    }
    //we need to check if there is no data and no writers (EOF)
    if (data == 0 && p->wio.refcnt == 0) {
        lock_release(&p->pipe_lock);
        return 0;
    }
    unsigned long to_read = bufsz;
    if (to_read > data) {
        to_read = data;
    }
    //we need to read the data from the ring buffer
    unsigned long head_to_end = p->size - p->head;
    //we need to read the data from the ring buffer
    if (to_read <= head_to_end) {
        memcpy(buf, p->buffer + p->head, to_read);
        p->head = (p->head + to_read) % p->size;
    } else {
        memcpy(buf, p->buffer + p->head, head_to_end);
        memcpy((char*)buf + head_to_end, p->buffer, to_read - head_to_end);
        p->head = to_read - head_to_end;
    }
    condition_broadcast(&p->space_avail);
    lock_release(&p->pipe_lock);
    return (long)to_read;
}

static void pipe_write_close(struct uio* uio) {
    struct pipe* p = (struct pipe*)((char*)uio - offsetof(struct pipe, wio));
    lock_acquire(&p->pipe_lock);
    //we need to wake up readers so they can notice wio.refcnt == 0 and see EOF
    condition_broadcast(&p->data_avail);
    if (p->wio.refcnt == 0 && p->rio.refcnt == 0) {
        char* buf = p->buffer;
        p->buffer = NULL;
        lock_release(&p->pipe_lock);
        free_phys_page(buf);
        kfree(p);
        return;
    }
    lock_release(&p->pipe_lock);
}

static void pipe_read_close(struct uio* uio) {
    struct pipe* p = (struct pipe*)((char*)uio - offsetof(struct pipe, rio));
    lock_acquire(&p->pipe_lock);
    //we need to wake up writers so they can see rio.refcnt == 0 and return -EPIPE
    condition_broadcast(&p->space_avail);
    if (p->wio.refcnt == 0 && p->rio.refcnt == 0) {
        char* buf = p->buffer;
        p->buffer = NULL;
        lock_release(&p->pipe_lock);
        free_phys_page(buf);
        kfree(p);
        return;
    }
    lock_release(&p->pipe_lock);
}
