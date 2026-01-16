
/*! @file memory.h
    @brief Physical and virtual memory manager
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA

*/

#ifndef _MEMORY_H_
#define _MEMORY_H_

#include <stddef.h>
#include <stdint.h>

#include "trap.h"  // for struct trap_frame

// EXPORTED CONSTANTS
//
#ifndef HEAP_ALLOC_MAX
#define HEAP_ALLOC_MAX (PAGE_SIZE - 64)
#endif

#define PAGE_ORDER 12
#define PAGE_SIZE (1UL << PAGE_ORDER)

// Flags for alloc_and_map_range() and set_range_flags()

#define PTE_V (1 << 0)  // internal use only
#define PTE_R (1 << 1)
#define PTE_W (1 << 2)
#define PTE_X (1 << 3)
#define PTE_U (1 << 4)
#define PTE_G (1 << 5)
#define PTE_A (1 << 6)  // internal use only
#define PTE_D (1 << 7)  // internal use only

// EXPORTED TYPE DEFINITIONS
//

// We refer to a memory space using an opaque memory space tag

typedef unsigned long mtag_t;

// EXPORTED FUNCTION DECLARATIONS
//

extern char memory_initialized;

/**
 * @brief Initializes kernel memory pages (with proper permissions), sets up
 * the heap memory manager, and adds remaining memory to the free chunk list
 * @return None
 */
extern void memory_init(void);

/**
 * @brief Gets the active memory space
 * @return The tag of the active memory space
 */
extern mtag_t active_mspace(void);

/**
 * @brief Switches the active memory space by writing the satp register
 * @param mtag Tag to write into satp
 * @return Tag that was in satp prior
 */
extern mtag_t switch_mspace(mtag_t mtag);

/**
 * @brief Copies all pages and page tables from the active memory space into
 * newly allocated memory
 * @return Tag corresponding to newly allocated memory
 */
extern mtag_t clone_active_mspace(void);

/**
 * @brief Unmaps and frees all non-global pages from the active memory space
 * @return None
 */
extern void reset_active_mspace(void);

/**
 * @brief Switches memory spaces to main, unmaps and frees all non-global pages
 * from the previously active memory space
 * @return Tag corresponding to main memory space
 */
extern mtag_t discard_active_mspace(void);

/**
 * @brief Adds page with provided virtual memory address and flags to page table
 * @param vma Virtual memory address for page (must be a PAGE_SIZE increment)
 * @param pp Pointer to page to be added to page table
 * @param rwxug_flags Flags to set on page
 * @return Newly mapped virtual memory address
 */
extern void* map_page(uintptr_t vma, void* pp, int rwxug_flags);

/**
 * @brief Adds a range of contiguous pages with provided virtual memory address, size, and flags to
 * page table
 * @param vma Virtual memory address for page (must be a PAGE_SIZE increment)
 * @param size Number of bytes to be mapped as pages
 * @param pp Pointer to the first page to be added to page table
 * @param rwxug_flags Flags to set on page
 * @return Newly mapped virtual memory address
 */
extern void* map_range(uintptr_t vma, size_t size, void* pp, int rwxug_flags);

/**
 * @brief Allocates memory for and maps a range of pages starting at provided virtual memory
 * address. Rounds up size to be a multiple of PAGE_SIZE
 * @param vma Virtual memory address to begin mapping at (must be a multiple of PAGE_SIZE)
 * @param size Size (in bytes) of range
 * @param rwxug_flags Flags to be set on pages in range
 * @return Newly mapped virtual memory address
 */
extern void* alloc_and_map_range(uintptr_t vma, size_t size, int rwxug_flags);

/**
 * @brief Sets passed flags for pages in range. Rounds up size to be a multiple of PAGE_SIZE.
 * @param vp Virtual memory address to begin setting flags at (must be a multiple of PAGE_SIZE)
 * @param size Size (in bytes) of range
 * @param rwxug_flags Flags to set
 * @return None
 */
extern void set_range_flags(const void* vp, size_t size, int rwxug_flags);

/**
 * @brief Unmaps a range of pages starting at provided virtual memory address and frees the pages.
 * Rounds up size to be a multiple of PAGE_SIZE.
 * @param vp Virtual memory address to begin unmapping at (must be a multiple of PAGE_SIZE)
 * @param size Size (in bytes) of range
 * @return None
 */
extern void unmap_and_free_range(void* vp, size_t size);

/**
 * @brief Checks that pointer is wellformed and pointer + len does not wrap around zero,
 * then iterates over pages in range, confirming the pages are mapped and have the passed
 * flags set.
 * @param vp Virtual memory address to start validation (must be a multiple of PAGE_SIZE)
 * @param len Size (in bytes) of range
 * @param rwxu_flags Flags to check pages in range for
 * @return 0 on success; error on malformed pointer, unmapped page, or mismatching flags
 */
extern int validate_vptr(const void* vp, size_t len, int rwxu_flags);

/**
 * @brief Checks that pointer is wellformed and the given string is valid. Since the length
 * of the string is unknown, we iterate through all characters of the string until \0 terminator,
 * confirming that the pages are mapped and have the passed flags set.
 * @param vs Virtual memory address that contains the string
 * @param rug_flags Flags to check pages
 * @return 0 on success; error on malformed pointer, unmapped page, or mismatching flags
 */
extern int validate_vstr(const char* vs, int rug_flags);

/**
 * @brief Allocates a single new page using alloc_phys_pages().
 * @return Address of the allocated page
 */
extern void* alloc_phys_page(void);

/**
 * @brief Free a page using free_phys_pages().
 * @param pp Physical address of page to free
 * @return None
 */
extern void free_phys_page(void* pp);

/**
 * @brief Allocates the passed number of physical pages from the free chunk list
 * @details Finds smallest chunk that fits the requested number of pages. If chunk
 * exactly matches the number of pages requested, removes chunk from free chunk list,
 * otherwise breaks off the component of chunk that matches requested number of pages.
 * Panics if no chunk can be found that satisfies the request.
 * @param cnt Number of pages to allocate
 * @return Pointer to allocated pages
 */
extern void* alloc_phys_pages(unsigned int cnt);

/**
 * @brief Adds chunk consisting of passed count of pages at passed pointer back to
 * free chunk list.
 * @param pp Physical address of memory region to add back to free chunk list
 * @param cnt Number of pages being freed
 * @return None
 */
extern void free_phys_pages(void* pp, unsigned int cnt);

/**
 * @brief Counts the number of pages remaining in the free chunk list.
 * @return Number of pages remaining in the free chunk list
 */
extern unsigned long free_phys_page_count(void);

/**
 * @brief Called by handle_umode_exception() in excp.c to
 * handle U mode load and store page faults. It returns 1 to indicate the fault
 * has been handled (the instruction should be restarted) and 0 to indicate that
 * the page fault is fatal and the process should be terminated.
 * @param tfr Trap frame for page fault (unused)
 * @param vma Virtual memory address that caused page fault
 * @return 1 if mapping was successful, 0 otherwise
 */
extern int handle_umode_page_fault(struct trap_frame* tfr, uintptr_t vma);

#endif
