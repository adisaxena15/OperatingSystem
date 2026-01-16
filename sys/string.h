/*! @file string.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief String and memory functions
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA

*/

#ifndef _STRING_H_
#define _STRING_H_

#include <stdarg.h>
#include <stddef.h>

/*!
 * @brief Return the length of the given string
 * @param s A string.
 * @return Length of the string.
 */
extern size_t strlen(const char *s);

/*!
 * @brief Compare if the two strings are same.
 * @details This function compares two strings. It will return 0 if they are equal,
 * -1 if s1 is shorter than s2, 1 if s1 is longer than s2. (if they are the same length,
 * the shorter string is the one that has the next character after their equal parts lower in the
 * ASCII values)
 * @param s1 First string.
 * @param s2 Second string.
 * @return 0 if they are equal, -1 if s1 is shorter than s2, 1 if s1 is longer than s2.
 */
extern int strcmp(const char *s1, const char *s2);

/*!
 * @brief Compare if the two strings are same up to length n.
 * @details This function compares two strings up to length n. It will return 0 if they are equal,
 * negative vlaues if s1 is shorter than s2, positive values if s1 is longer than s2. (if they are
 * the same length, the shorter string is the one that has the next character after their equal
 * parts lower in the ASCII values)
 * @param s1 First string.
 * @param s2 Second string.
 * @param n Maximum length to compare to.
 * @return 0 if they are equal, negative values if s1 is shorter than s2, positive values if s1 is
 * longer than s2.
 */
extern int strncmp(const char *s1, const char *s2, size_t n);

/*!
 * @brief Deep copy the string from src to dst up to length n
 * @param dst Pointer to the destination string.
 * @param src Pointer to the source string.
 * @param n Maximum length to copy to.
 * @return The copied stirng.
 */
extern char *strncpy(char *dst, const char *src, size_t n);

/*!
 * @brief Search for the first occurence of a chracter in a stirng.
 * @param s A string.
 * @param c A chracter.
 * @return Pointer to the first occurence of the chracter, NULL if not found.
 */
extern char *strchr(const char *s, int c);

/*!
 * @brief Search for the last occurence of a chracter in a stirng.
 * @param s A string.
 * @param c A chracter.
 * @return Pointer to the last occurence of the chracter, NULL if not found.
 */
extern char *strrchr(const char *s, int c);

/*!
 * @brief Set the memory pointed by s to c for length n.
 * @param s The pointer.
 * @param c Content to set the memory to.
 * @param n Length of the memory to set.
 * @return  The pointer that points to the set memory.
 */
extern void *memset(void *s, int c, size_t n);

/*!
 * @brief Compare if two memory region contain the same content.
 * @param p1 Pointer to the first memory region.
 * @param p2 Pointer to the second memory region.
 * @param n Length of the memory region.
 * @return 0 if they are the same, negative if p1's first different content is samller, positive
 * otherwise.
 */
extern int memcmp(const void *p1, const void *p2, size_t n);

/*!
 * @brief Deep copy from one memory region to another.
 * @param dst Pointer to the destination.
 * @param src Pointer to the Source.
 * @param n Length of the memory region.
 * @return Pointer to the destination.
 */
extern void *memcpy(void *restrict dst, const void *restrict src, size_t n);

/*!
 * @brief Convert a string of a certain base to unsigned integer values. (currently works for base =
 * [0, 10])
 * @param str A string.
 * @param endptr The remaining part of the string that's not number.
 * @param base Base of the number represented by the string.
 * @return Integer value represetned the string.
 */
extern unsigned long strtoul(const char *str, char **endptr, int base);

/*!
 * @brief Print a string into a buffer of a specified size with a specified format.
 * @details This function will parse through the optional arguments into a va_list and internally
 * call vsnprintf.
 * @param buf Pointer to a buffer.
 * @param bufsz Maximum length of the string that will be printed into buf.
 * @param fmt The string containing the format.
 * @param ... Optional arguments for formatting.
 * @return Number of characters written into buf.
 */
extern size_t snprintf(char *buf, size_t bufsz, const char *fmt, ...);

/*!
 * @brief Print a string into a buffer of a specified size with a specified format.
 * @details This function will use a vsnprintf_state to track the current position and remaining
 * space in buf. It will internally call vgprintf.
 * @param buf Pointer to a buffer.
 * @param bufsz Maximum length of the string that will be printed into buf.
 * @param fmt The string containing the format.
 * @param ap Optional arguments for formatting.
 * @return Number of characters written into buf.
 */
extern size_t vsnprintf(char *buf, size_t bufsz, const char *fmt, va_list ap);

/*!
 * @brief Print a string into a buffer of a specified size with a specified format.
 * @details This is the core function that will parse through the string and the format. It will
 * call specific putc function to put chracters.
 * @param putcfn putc function that will be called by this function.
 * @param aux Auxiliary pointer that tracks the state of buf.
 * @param fmt The string containing the format.
 * @param ap Optional arguments for formatting.
 * @return Number of characters written.
 */
extern size_t vgprintf(void (*putcfn)(char, void *), void *aux, const char *fmt, va_list ap);

#endif  // _STRING_H_