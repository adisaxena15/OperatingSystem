/*! @file misc.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Miscellaneous functions
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#include "misc.h"

#include <stdarg.h>
#include <stddef.h>

#include "console.h"
#include "intr.h"
#include "see.h"  // for halt_failure()

// EXPORTED FUNCTION DEFINITIONS
//

void panic_actual(const char* filename, int lineno, const char* msg) {
    if (msg != NULL && *msg != '\0')
        kprintf("PANIC at %s:%d: %s\n", filename, lineno, msg);
    else
        kprintf("PANIC at %s:%d", filename, lineno);

    halt_failure();
}

void assert_failed(const char* filename, int lineno, const char* stmt) {
    kprintf("ASSERT FAILED at %s:%d (%s)\n", filename, lineno, stmt);
    halt_failure();
}

void debug_actual(const char* filename, int lineno, const char* fmt, ...) {
    va_list ap;
    int pie;

    va_start(ap, fmt);
    pie = disable_interrupts();

    kprintf("DEBUG at %s:%d: ", filename, lineno);
    kvprintf(fmt, ap);
    kprintf("\n");

    restore_interrupts(pie);
    va_end(ap);
}

void trace_actual(const char* filename, int lineno, const char* fmt, ...) {
    va_list ap;
    int pie;

    va_start(ap, fmt);
    pie = disable_interrupts();

    kprintf("TRACE at %s:%d: ", filename, lineno);
    kvprintf(fmt, ap);
    kprintf("\n");

    restore_interrupts(pie);
    va_end(ap);
}