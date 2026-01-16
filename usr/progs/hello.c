#ifndef UMODE // cp1
    #include "uio.h"
    void main(struct uio * uio) {
        uio_printf(uio, "Hello, world!\n");
    }
#endif

#ifdef UMODE // cp2&3
    #include "string.h"
    void main(void) {
        printf("Hello, world!\n");
    }
#endif