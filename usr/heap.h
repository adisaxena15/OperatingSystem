// heap.h - User heap memory allocator
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

/*! @file heap.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief User heap memory allocator
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA
*/

#include <stddef.h>

/**
 *  @brief User heap initialization state
 */
extern char heap_initialized;

/**
 * @brief Initializes a region of user memory for user heap use.
 * @param start Pointer to a location of user memory that the heap will start at.
 * @param end Pointer to a location of user memory that the heap ends at.
 * @return None
 */
extern void heap_init(void * start, void * end);

/**
 * @brief Allocates a region of memory on the user heap of size number of bytes.
 * @param size Number of bytes that is desired to be allocated.
 * @return A void pointer to the allocated space, NULL if insufficient memory avalible.
 */
extern void * malloc(size_t size);

/**
 * @brief Allocates memory on the heap of nelts * eltsz number of bytes. Initializes all of the allocated memory to zero's.
 * @param nelts Number of elements that are desired to be allocated.
 * @param eltsz Size of each element in bytes.
 * @return A void pointer to the allocated space, NULL if insufficient memory avalible.
 */
extern void * calloc(size_t nelts, size_t eltsz);

/**
 * @brief Allocates memory on the heap of nelts * eltsz number of bytes. Initializes all of the allocated memory to zero's.
 * @param nelts Number of elements that are desired to be allocated.
 * @param eltsz Size of each element in bytes.
 * @return A void pointer to the allocated space, NULL if insufficient memory avalible.
 */
extern void free(void * ptr);
