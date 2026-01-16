/*! @file misc.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Miscellaneous functions
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifndef _MISC_H_
#define _MISC_H_

// The ROUND_UP and ROUND_DOWN macros round /n/ to a multiple of /k/. Argument
// /k/ is evaluated multiple times.

#define ROUND_UP(n, k) (((n) + (k) - 1) / (k) * (k))
#define ROUND_DOWN(n, k) ((n) / (k) * (k))

// The MIN and MAX macros find the minumum and maximum between /a/ and /b/
// respectively.
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

// The ISPOW2 macro evaluates to 1 if its argument is either zero or a power of
// two. The argument must be an integer type. Cast pointers to `uintptr_t` to
// test pointer alignment. The argument is evaluated multiple times.

#define ISPOW2(n) (((n) & ((n) - 1)) == 0)

// The panic() macro prints a message to the console and halts the system.

extern void __attribute__((noreturn)) panic_actual(const char* filename, int lineno,
                                                   const char* msg);

#define panic(msg)                               \
    do {                                         \
        panic_actual(__FILE__, __LINE__, (msg)); \
    } while (0)

// The assert() macro tests if a statement is true. If it is not, it prints an
// assertion error to the console and halts the system.

extern void __attribute__((noreturn)) assert_failed(const char* filename, int lineno,
                                                    const char* stmt);

#define assert(c)                                  \
    do {                                           \
        if (!(c)) {                                \
            assert_failed(__FILE__, __LINE__, #c); \
        }                                          \
    } while (0)

void debug_actual(const char* filename, int lineno, const char* fmt, ...);
void trace_actual(const char* filename, int lineno, const char* fmt, ...);

#ifdef DEBUG
#define debug(...) debug_actual(__FILE__, __LINE__, __VA_ARGS__)
#else
#define debug(...) \
    do {           \
    } while (0)
#endif

#ifdef TRACE
#define trace(...) trace_actual(__FILE__, __LINE__, __VA_ARGS__)
#else
#define trace(...) \
    do {           \
    } while (0)
#endif

#endif  // _MISC_H_