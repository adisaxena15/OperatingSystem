// uio.c - Abstract uio interface
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

/*! @file uio.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Uniform I/O object
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA
    
*/
#include "uio.h"
#include "error.h"

#include <stddef.h>

// INTERNAL TYPE DEFINITIONS
//

struct uiovprintf_state {
    struct uio * uio;
    int err;
};

// INTERNAL FUNCTION DECLARATIONS
//

/**
 * @brief Closes the "raw" Uniform I/O object
 * @param uio The Uniform I/O abstraction to interact with
 * @return None
 */
static void uioterm_close(struct uio * uio);

/**
 * @brief Reads from the "raw" Uniform I/O object with input CRLF normalization
 * @param uio The Uniform I/O abstraction to interact with
 * @param buf Buffer to copy data into
 * @param len Size of passed buffer in bytes
 * @return Number of bytes read
 */
static long uioterm_read(struct uio * uio, void * buf, unsigned long len);

/**
 * @brief Writes to the "raw" Uniform I/O object with output CRLF normalization
 * @param uio The Uniform I/O abstraction to interact with
 * @param buf Buffer to copy data from
 * @param len Size of passed buffer in bytes
 * @return Number of bytes written
 */
static long uioterm_write(struct uio * uio, const void * buf, unsigned long len);

/**
 * @brief Passes fcntls through to the backing "raw" Uniform I/O interface
 * @param uio The Uniform I/O abstraction to interact with
 * @param cmd Operation to perform
 * @param arg Additional argument for operation (if needed)
 * @return Output of _fcntl_ if successfully called, negative error code otherwise 
 */
static int uioterm_cntl(struct uio * uio, int cmd, void * arg);

/**
 * @brief Writes a character to the backing Uniform I/O if it is not in error state
 * @param c The character to be written
 * @param arg Pointer holding reference to the Uniform I/O abstraction and it's error state
 * @return None
 */
static void uio_vprintf_putc(char c, void * aux);

// EXPORTED FUNCTION DEFINITIONS
//

unsigned long uio_refcnt(const struct uio * uio) {
    return uio->refcnt;
}

int uio_addref(struct uio * uio) {
    return ++uio->refcnt;
}

void uio_close(struct uio * uio) {
  if (uio->refcnt > 0)
  {
    uio->refcnt--;
  }

  if (uio->refcnt == 0 && uio->intf->close != NULL)
  {
    uio->intf->close(uio);
  }
}

long uio_read(struct uio * uio, void * buf, unsigned long bufsz) {
    if (uio->intf->read != NULL) {
        if (0 <= (long)bufsz)
            return uio->intf->read(uio, buf, bufsz);
        else
            return -EINVAL;
    } else
        return -ENOTSUP;
}

long uio_write(struct uio * uio, const void * buf, unsigned long buflen) {
    if (uio->intf->write != NULL) {
        if (0 <= (long)buflen)
            return uio->intf->write(uio, buf, buflen);
        else
            return -EINVAL;
    } else
        return -ENOTSUP;
}

int uio_cntl(struct uio * uio, int op, void * arg) {
    if (uio->intf->cntl != NULL)
        return uio->intf->cntl(uio, op, arg);
    else
        return -ENOTSUP;
}

int uio_puts(struct uio * uio, const char * s) {
    const char nl = '\n';
    size_t slen;
    long wlen;

    slen = strlen(s);

    wlen = uio_write(uio, s, slen);
    if (wlen < 0)
        return wlen;

    // Write newline

    wlen = uio_write(uio, &nl, 1);
    if (wlen < 0)
        return wlen;
    
    return 0;
}

long uio_printf(struct uio * uio, const char * fmt, ...) {
	va_list ap;
	long result;

	va_start(ap, fmt);
	result = uio_vprintf(uio, fmt, ap);
	va_end(ap);
	return result;
}

long uio_vprintf(struct uio * uio, const char * fmt, va_list ap) {
    // state.nout is number of chars written or negative error code
    struct uiovprintf_state state = { .uio = uio, .err = 0 };
    size_t nout;

	nout = vgprintf(uio_vprintf_putc, &state, fmt, ap);
    return state.err ? state.err : nout;
}

struct uio * uioterm_init(struct uioterm * uiot, struct uio * rawuio) {
    static const struct uio_intf ops = {
        .close = uioterm_close,
        .read = uioterm_read,
        .write = uioterm_write,
        .cntl = uioterm_cntl
    };

    uiot->uio.intf = &ops;
    uiot->rawuio = rawuio;
    uiot->cr_out = 0;
    uiot->cr_in = 0;

    return &uiot->uio;
};

char * uioterm_getsn(struct uioterm * uiot, char * buf, size_t n) {
    char * p = buf;
    int result;
    char c;

    for (;;) {
        c = uio_getc(&uiot->uio); // already CRLF normalized

        switch (c) {
        case '\133': // escape
            uiot->cr_in = 0;
            break;
        case '\r': // should not happen      
        case '\n':
            result = uio_putc(uiot->rawuio, '\r');
            if (result < 0)
                return NULL;
            result = uio_putc(uiot->rawuio, '\n');
            if (result < 0)
                return NULL;
            *p = '\0';
            return buf;
        case '\b': // backspace
        case '\177': // delete
            if (p != buf) {
                p -= 1;
                n += 1;
                
                result = uio_putc(uiot->rawuio, '\b');
                if (result < 0)
                    return NULL;
                result = uio_putc(uiot->rawuio, ' ');
                if (result < 0)
                    return NULL;
                result = uio_putc(uiot->rawuio, '\b');
            } else
                result = uio_putc(uiot->rawuio, '\a'); // beep
            
            if (result < 0)
                return NULL;
            break;

        default:
            if (n > 1) {
                result = uio_putc(uiot->rawuio, c);
                *p++ = c;
                n -= 1;
            } else
                result = uio_putc(uiot->rawuio, '\a'); // beep
            
            if (result < 0)
                return NULL;
        }
    }
}

void uioterm_close(struct uio * uio) {
    struct uioterm * const uiot = (void*)uio - offsetof(struct uioterm, uio);
    uio_close(uiot->rawuio);
}

long uioterm_read(struct uio * uio, void * buf, unsigned long len) {
    struct uioterm * const uiot = (void*)uio - offsetof(struct uioterm, uio);
    char * rp;
    char * wp;
    long cnt;
    char ch;

    do {
        // Fill buffer using backing uio interface

        cnt = uio_read(uiot->rawuio, buf, len);

        if (cnt < 0)
            return cnt;
        
        // Scan though buffer and fix up line endings. We may end up removing some
        // characters from the buffer.  We maintain two pointers /wp/ (write
        // position) and and /rp/ (read position). Initially, rp = wp, however, as
        // we delete characters, /rp/ gets ahead of /wp/, and we copy characters
        // from *rp to *wp to shift the contents of the buffer.
        // 
        // The processing logic is as follows:
        // if cr_in = 0 and ch == '\r': return '\n', cr_in <- 1;
        // if cr_in = 0 and ch != '\r': return ch;
        // if cr_in = 1 and ch == '\r': return \n;
        // if cr_in = 1 and ch == '\n': skip, cr_in <- 0;
        // if cr_in = 1 and ch != '\r' and ch != '\n': return ch, cr_in <- 0.

        wp = rp = buf;
        while ((void*)rp < buf+cnt) {
            ch = *rp++;

            if (uiot->cr_in) {
                switch (ch) {
                case '\r':
                    *wp++ = '\n';
                    break;
                case '\n':
                    uiot->cr_in = 0;
                    break;
                default:
                    uiot->cr_in = 0;
                    *wp++ = ch;
                }
            } else {
                switch (ch) {
                case '\r':
                    uiot->cr_in = 1;
                    *wp++ = '\n';
                    break;
                default:
                    *wp++ = ch;
                }
            }
        }

    // We need to return at least one character, however, it is possible that
    // the buffer is still empty. (This would happen if it contained a single
    // '\n' character and cr_in = 1.) If this happens, read more characters.
    } while (wp == buf);

    return (wp - (char*)buf);
}

long uioterm_write(struct uio * uio, const void * buf, unsigned long len) {
    struct uioterm * const uiot = (void*)uio - offsetof(struct uioterm, uio);
    long acc = 0; // how many bytes from the buffer have been written
    const char * wp;  // everything up to /wp/ in buffer has been written out
    const char * rp;  // position in buffer we're reading
    long cnt;
    char ch;

    // Scan through buffer and look for cases where we need to modify the line
    // ending: lone \r and lone \n get converted to \r\n, while existing \r\n
    // are not modified. We can't modify the buffer, so mwe may need to do
    // partial writes.
    // The strategy we want to implement is:
    // if cr_out = 0 and ch == '\r': output \r\n to rawuio, cr_out <- 1;
    // if cr_out = 0 and ch == '\n': output \r\n to rawuio;
    // if cr_out = 0 and ch != '\r' and ch != '\n': output ch to rawuio;
    // if cr_out = 1 and ch == '\r': output \r\n to rawuio;
    // if cr_out = 1 and ch == '\n': no ouput, cr_out <- 0;
    // if cr_out = 1 and ch != '\r' and ch != '\n': output ch, cr_out <- 0.

    wp = rp = buf;

    while ((void*)rp < buf+len) {
        ch = *rp++;
        switch (ch) {
        case '\r':
            // We need to emit a \r\n sequence. If it already occurs in the
            // buffer, we're all set. Otherwise, we need to write what we have
            // from the buffer so far, then write \n, and then continue.
            if ((void*)rp < buf+len && *rp == '\n') {
                // The easy case: buffer already contains \r\n, so keep going.
                uiot->cr_out = 0;
                rp += 1;
            } else {
                // Next character is not '\n' or we're at the end of the buffer.
                // We need to write out what we have so far and add a \n.
                cnt = uio_write(uiot->rawuio, wp, rp - wp);
                if (cnt < 0)
                    return cnt;
                else if (cnt == 0)
                    return acc;
                
                acc += cnt;
                wp += cnt;

                // Now output \n, which does not count toward /acc/.
                cnt = uio_putc(uiot->rawuio, '\n');
                if (cnt < 0)
                    return cnt;
                
                uiot->cr_out = 1;
            }
                
            break;
        
        case '\n':
            // If last character was \r, skip the \n. This should only occur at
            // the beginning of the buffer, because we check for a \n after a
            // \r, except if \r is the last character in the buffer. Since we're
            // at the start of the buffer, we don't have to write anything out.
            if (uiot->cr_out) {
                uiot->cr_out = 0;
                wp += 1;
                break;
            }
            
            // Previous character was not \r, so we need to write a \r first,
            // then the rest of the buffer. But before that, we need to write
            // out what we have so far, up to, but not including the \n we're
            // processing.
            if (wp != rp-1) {
                cnt = uio_write(uiot->rawuio, wp, rp-1 - wp);
                if (cnt < 0)
                    return cnt;
                else if (cnt == 0)
                    return acc;
                acc += cnt;
                wp += cnt;
            }
            
            cnt = uio_putc(uiot->rawuio, '\r');
            if (cnt < 0)
                return cnt;
            
            // wp should now point to \n. We'll write it when we drain the
            // buffer later.

            uiot->cr_out = 0;
            break;
            
        default:
            uiot->cr_out = 0;
        }
    }

    if (rp != wp) {
        cnt = uio_write(uiot->rawuio, wp, rp - wp);

        if (cnt < 0)
            return cnt;
        else if (cnt == 0)
            return acc;
        acc += cnt;
    }

    return acc;
}

int uioterm_cntl(struct uio * uio, int cmd, void * arg) {
    struct uioterm * const uiot = (void*)uio - offsetof(struct uioterm, uio);

    // Pass ufcntls through to backing uio interface. Seeking is not supported,
    // because we maintain state on the characters output so far.
    if (cmd != FCNTL_SETPOS)
        return uio_cntl(uiot->rawuio, cmd, arg);
    else
        return -ENOTSUP;
}

void uio_vprintf_putc(char c, void * aux) {
    struct uiovprintf_state * const state = aux;
    int result;

    if (state->err == 0) {
        result = uio_putc(state->uio, c);
        if (result < 0)
            state->err = result;
    }
}
