/*! @file heap.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Heap memory manager
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifndef _HEAP_H_
#define _HEAP_H_

#include <stddef.h>

#include "memory.h"

// Maximum allocation request size

#ifndef HEAP_ALLOC_MAX
#define HEAP_ALLOC_MAX 4000
#endif

extern char heap_initialized;  // 1 if heap_init called, 0 otherwise

extern void heap_init(void* start, void* end);

/**
 * @brief Allocates a region of memory on the heap of size number of bytes.
 * @param size Number of bytes that is desired to be allocated.
 * @return A void pointer to the allocated space, NULL if insufficient memory avalible.
 */
extern void* kmalloc(size_t size);

/**
 * @brief Allocates memory on the heap of nelts * eltsz number of bytes. Initializes all of the
 * allocated memory to zero's.
 * @param nelts Number of elements that are desired to be allocated.
 * @param eltsz Size of each element in bytes.
 * @return A void pointer to the allocated space, NULL if insufficient memory avalible.
 */
extern void* kcalloc(size_t nelts, size_t eltsz);

/**
 * @brief Deallocate memory on the heap that was previously allocated.
 * @param ptr A pointer to the begining of the previously allocated block of memory on the heap.
 * @return None
 */
extern void kfree(void* ptr);

#endif  // _HEAP_H_