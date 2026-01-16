/*! @file console.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Console i/o
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include <stdarg.h>
#include <stddef.h>

#include "see.h"

extern char console_initialized;

extern void console_init(void);

extern void kputc(char c);

extern char kgetc(void);

extern void kputs(const char* str);

extern char* kgetsn(char* buf, size_t n);

extern void kprintf(const char* fmt, ...);

extern void kvprintf(const char* fmt, va_list ap);

// The following must be defined elsewhere, to be used for console I/O.
// Currently, they are provided in uart.c using the NS8250 UART.

extern void console_device_init(void);
extern void console_device_putc(char c);
extern char console_device_getc(void);

#endif  // _CONSOLE_H_