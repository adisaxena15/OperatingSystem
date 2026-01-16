
/*! @file memory.c
    @brief Physical and virtual memory manager
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA

*/

#ifdef MEMORY_TRACE
#define TRACE
#endif

#ifdef MEMORY_DEBUG
#define DEBUG
#endif

#include "memory.h"

#include "conf.h"
#include "console.h"
#include "error.h"
#include "heap.h"
#include "misc.h"
#include "process.h"
#include "riscv.h"
#include "string.h"
#include "thread.h"

// COMPILE-TIME CONFIGURATION
//

// Minimum amount of memory in the initial heap block.

#ifndef HEAP_INIT_MIN
#define HEAP_INIT_MIN 256
#endif

// INTERNAL CONSTANT DEFINITIONS
//

#define MEGA_SIZE ((1UL << 9) * PAGE_SIZE)  // megapage size
#define GIGA_SIZE ((1UL << 9) * MEGA_SIZE)  // gigapage size

#define PTE_ORDER 3
#define PTE_CNT (1U << (PAGE_ORDER - PTE_ORDER))

#ifndef PAGING_MODE
#define PAGING_MODE RISCV_SATP_MODE_Sv39
#endif

#ifndef ROOT_LEVEL
#define ROOT_LEVEL 2
#endif

// IMPORTED GLOBAL SYMBOLS
//

// linker-provided (kernel.ld)
extern char _kimg_start[];
extern char _kimg_text_start[];
extern char _kimg_text_end[];
extern char _kimg_rodata_start[];
extern char _kimg_rodata_end[];
extern char _kimg_data_start[];
extern char _kimg_data_end[];
extern char _kimg_end[];

// EXPORTED GLOBAL VARIABLES
//

char memory_initialized = 0;

// INTERNAL TYPE DEFINITIONS
//

// We keep free physical pages in a linked list of _chunks_, where each chunk
// consists of several consecutive pages of memory. Initially, all free pages
// are in a single large chunk. To allocate a block of pages, we break up the
// smallest chunk on the list.

/**
 * @brief Section of consecutive physical pages. We keep free physical pages in a
 * linked list of chunks. Initially, all free pages are in a single large chunk. To
 * allocate a block of pages, we break up the smallest chunk in the list
 */
struct page_chunk {
    struct page_chunk *next;  ///< Next page in list
    unsigned long pagecnt;    ///< Number of pages in chunk
};

/**
 * @brief RISC-V PTE. RTDC (RISC-V docs) for what each of these fields means!
 */
struct pte {
    uint64_t flags : 8;
    uint64_t rsw : 2;
    uint64_t ppn : 44;
    uint64_t reserved : 7;
    uint64_t pbmt : 2;
    uint64_t n : 1;
};

// INTERNAL MACRO DEFINITIONS
//

#define VPN(vma) ((vma) / PAGE_SIZE)
#define VPN2(vma) ((VPN(vma) >> (2 * 9)) % PTE_CNT)
#define VPN1(vma) ((VPN(vma) >> (1 * 9)) % PTE_CNT)
#define VPN0(vma) ((VPN(vma) >> (0 * 9)) % PTE_CNT)

// The following macros test is a PTE is valid, global, or a leaf. The argument
// is a struct pte (*not* a pointer to a struct pte).

#define PTE_VALID(pte) (((pte).flags & PTE_V) != 0)
#define PTE_GLOBAL(pte) (((pte).flags & PTE_G) != 0)
#define PTE_LEAF(pte) (((pte).flags & (PTE_R | PTE_W | PTE_X)) != 0)

#define PT_INDEX(lvl, vpn) \
    (((vpn) & (0x1FF << (lvl * (PAGE_ORDER - PTE_ORDER)))) >> (lvl * (PAGE_ORDER - PTE_ORDER)))
// INTERNAL FUNCTION DECLARATIONS
//

static void ptab_reset(struct pte *ptab  // page table to reset
);

static struct pte *ptab_clone(struct pte *ptab  // page table to clone
);

static void ptab_discard(struct pte *ptab  // page table to discard
);

static void ptab_insert(struct pte *ptab,   // page table to modify
                        unsigned long vpn,  // virtual page number to insert
                        void *pp,           // pointer to physical page to insert
                        int rwxug_flags     // flags for inserted mapping
);

static void *ptab_remove(struct pte *ptab, unsigned long vpn);

static void ptab_adjust(struct pte *ptab, unsigned long vpn, int rwxug_flags);

struct pte *ptab_fetch(struct pte *ptab, unsigned long vpn);

static inline mtag_t active_space_mtag(void);
static inline mtag_t ptab_to_mtag(struct pte *root, unsigned int asid);
static inline struct pte *mtag_to_ptab(mtag_t mtag);
static inline struct pte *active_space_ptab(void);

static inline void *pageptr(uintptr_t n);
static inline uintptr_t pagenum(const void *p);
static inline int wellformed(uintptr_t vma);

static inline struct pte leaf_pte(const void *pp, uint_fast8_t rwxug_flags);
static inline struct pte ptab_pte(const struct pte *pt, uint_fast8_t g_flag);
static inline struct pte null_pte(void);

struct pte *ptab_fetch(struct pte *ptab, unsigned long vpn) {
    struct pte *pte;
    int level;
    for (level = ROOT_LEVEL; level > 0; level--) {
        unsigned int index = PT_INDEX(level, vpn);
        pte = &ptab[index];
        if (!PTE_VALID(*pte)) {
            return NULL;
        }
        if (PTE_LEAF(*pte)) {
            return pte;
        }

        ptab = (struct pte *)pageptr(pte->ppn);
    }
    pte = &ptab[PT_INDEX(0, vpn)];
    return PTE_VALID(*pte) ? pte : NULL;
}
static void ptab_insert(struct pte *ptab, unsigned long vpn, void *pp, int rwxug_flags) {
    struct pte *pte;
    struct pte *next_ptab;
    int level;
    trace("%s(%p, %lu, %p, %d)", __func__, ptab, vpn, pp, rwxug_flags);
    for (level = ROOT_LEVEL; level > 0; level--) {
        unsigned int index = PT_INDEX(level, vpn);
        pte = &ptab[index];
        if (!PTE_VALID(*pte)) {
            next_ptab = (struct pte *)alloc_phys_page();
            *pte = ptab_pte(next_ptab, rwxug_flags & PTE_G);
        } else if (PTE_LEAF(*pte)) {
            panic("ptab_insert: already mapped");
        }
        ptab = (struct pte *)pageptr(pte->ppn);
    }
    pte = &ptab[PT_INDEX(0, vpn)];
    if (PTE_VALID(*pte)) {
        panic("ptab_insert: already mapped");
    }
    *pte = leaf_pte(pp, rwxug_flags);
}
static void *ptab_remove(struct pte *ptab, unsigned long vpn) {
    struct pte *pte;
    void *pp;
    trace("%s(%p, %lu)", __func__, ptab, vpn);
    pte = ptab_fetch(ptab, vpn);
    if (pte == NULL || !PTE_VALID(*pte)) {
        return NULL;
    }
    pp = pageptr(pte->ppn);
    *pte = null_pte();
    return pp;
}
static void ptab_adjust(struct pte *ptab, unsigned long vpn, int rwxug_flags) {
    struct pte *pte;
    trace("%s(%p, %lu, %d)", __func__, ptab, vpn, rwxug_flags);
    pte = ptab_fetch(ptab, vpn);
    if (pte == NULL || !PTE_VALID(*pte)) {
        panic("ptab_adjust: not mapped");
    }
    pte->flags = rwxug_flags | PTE_A | PTE_D | PTE_V;
}
static void ptab_reset(struct pte *ptab) {
    int i;

    trace("%s(%p)", __func__, ptab);
    for (i = 0; i < PTE_CNT; i++) {
        struct pte *pte = &ptab[i];
        if (!PTE_VALID(*pte)) continue;
        if (PTE_GLOBAL(*pte)) continue;
        if (PTE_LEAF(*pte)) {
            free_phys_page(pageptr(pte->ppn));
        } else {
            struct pte *pt1 = (struct pte *)pageptr(pte->ppn);
            int j;
            for (j = 0; j < PTE_CNT; j++) {
                struct pte *pte1 = &pt1[j];
                if (!PTE_VALID(*pte1)) continue;
                if (PTE_GLOBAL(*pte1)) continue;
                if (PTE_LEAF(*pte1)) {
                    free_phys_page(pageptr(pte1->ppn));
                } else {
                    struct pte *pt0 = (struct pte *)pageptr(pte1->ppn);
                    int k;
                    for (k = 0; k < PTE_CNT; k++) {
                        struct pte *pte0 = &pt0[k];
                        if (PTE_VALID(*pte0) && !PTE_GLOBAL(*pte0)) {
                            free_phys_page(pageptr(pte0->ppn));
                            *pte0 = null_pte();
                        }
                    }
                    free_phys_page(pt0);
                }
                *pte1 = null_pte();
            }
            free_phys_page(pt1);
        }
        *pte = null_pte();
    }
}
static void ptab_discard(struct pte *ptab) {
    trace("%s(%p)", __func__, ptab);
    ptab_reset(ptab);
    free_phys_page(ptab);
}
static struct pte *ptab_clone(struct pte *ptab) {
    struct pte *new_ptab;
    int i;
    trace("%s(%p)", __func__, ptab);
    //we need to allocate a new page table
    new_ptab = (struct pte *)alloc_phys_page();
    if (new_ptab == NULL) {
    return NULL;
    }
    //we need to iterate through all entries in the page table
    for (i = 0; i < PTE_CNT; i++) {
        struct pte *pte = &ptab[i];
        //we need to skip invalid or global entries (global pages are shared, not copied)
        if (!PTE_VALID(*pte) || PTE_GLOBAL(*pte)) {
            new_ptab[i] = *pte;
            continue;
        }
        //we need to handle leaf PTEs (actual pages)
        if (PTE_LEAF(*pte)) {
            //we need to allocate a new physical page
            void *old_page = pageptr(pte->ppn);
            void *new_page = alloc_phys_page();
            if (new_page == NULL) {
                //we need to clean up and return NULL on failure
                //we need to free all pages we've allocated so far
                for (int j = 0; j < i; j++) {
                    if (PTE_VALID(new_ptab[j]) && !PTE_GLOBAL(new_ptab[j]) && PTE_LEAF(new_ptab[j])) {
                        free_phys_page(pageptr(new_ptab[j].ppn));
                    }
                }
                //we need to free the new page table
                free_phys_page(new_ptab);
                return NULL;
            }
            //we need to copy the page contents
            memcpy(new_page, old_page, PAGE_SIZE);
            //we need to create a new PTE pointing to the copied page
            new_ptab[i] = leaf_pte(new_page, pte->flags);
        } else {
            //we need to handle non-leaf PTEs (subtables)
            struct pte *old_subtab = (struct pte *)pageptr(pte->ppn);
            struct pte *new_subtab = ptab_clone(old_subtab);
            if (new_subtab == NULL) {
                //we need to clean up on failure
                for (int j = 0; j < i; j++) {
                    if (PTE_VALID(new_ptab[j]) && !PTE_GLOBAL(new_ptab[j])) {
                        if (PTE_LEAF(new_ptab[j])) {
                            free_phys_page(pageptr(new_ptab[j].ppn));
                        } else {
                            //we need to discard the subtable
                            ptab_discard((struct pte *)pageptr(new_ptab[j].ppn));
                        }
                    }
                }
                //we need to free the new page table
                free_phys_page(new_ptab);
                return NULL;
            }
            //we need to create a new PTE pointing to the cloned subtable
            new_ptab[i] = ptab_pte(new_subtab, pte->flags & PTE_G);
        }
    }
    return new_ptab;
}

// INTERNAL GLOBAL VARIABLES
//

static mtag_t main_mtag;

static struct pte main_pt2[PTE_CNT] __attribute__((section(".bss.pagetable"), aligned(4096)));

static struct pte main_pt1_0x80000[PTE_CNT]
    __attribute__((section(".bss.pagetable"), aligned(4096)));

static struct pte main_pt0_0x80000[PTE_CNT]
    __attribute__((section(".bss.pagetable"), aligned(4096)));

static struct page_chunk *free_chunk_list;

// EXPORTED FUNCTION DECLARATIONS
//

void memory_init(void) {
    const void *const text_start = _kimg_text_start;
    const void *const text_end = _kimg_text_end;
    const void *const rodata_start = _kimg_rodata_start;
    const void *const rodata_end = _kimg_rodata_end;
    const void *const data_start = _kimg_data_start;

    void *heap_start;
    void *heap_end;

    uintptr_t pma;
    const void *pp;

    trace("%s()", __func__);

    assert(RAM_START == _kimg_start);

    debug("           RAM: [%p,%p): %zu MB", RAM_START, RAM_END, RAM_SIZE / 1024 / 1024);
    debug("  Kernel image: [%p,%p)", _kimg_start, _kimg_end);

    // Kernel must fit inside 2MB megapage (one level 1 PTE)

    if (MEGA_SIZE < _kimg_end - _kimg_start) panic(NULL);

    // Initialize main page table with the following direct mapping:
    //
    //         0 to RAM_START:           RW gigapages (MMIO region)
    // RAM_START to _kimg_end:           RX/R/RW pages based on kernel image
    // _kimg_end to RAM_START+MEGA_SIZE: RW pages (heap and free page pool)
    // RAM_START+MEGA_SIZE to RAM_END:   RW megapages (free page pool)
    //
    // RAM_START = 0x80000000
    // MEGA_SIZE = 2 MB
    // GIGA_SIZE = 1 GB

    // Identity mapping of MMIO region as two gigapage mappings
    for (pma = 0; pma < RAM_START_PMA; pma += GIGA_SIZE)
        main_pt2[VPN2(pma)] = leaf_pte((void *)pma, PTE_R | PTE_W | PTE_G);

    // Third gigarange has a second-level subtable
    main_pt2[VPN2(RAM_START_PMA)] = ptab_pte(main_pt1_0x80000, PTE_G);

    // First physical megarange of RAM is mapped as individual pages with
    // permissions based on kernel image region.

    main_pt1_0x80000[VPN1(RAM_START_PMA)] = ptab_pte(main_pt0_0x80000, PTE_G);

    for (pp = text_start; pp < text_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_X | PTE_G);
    }

    for (pp = rodata_start; pp < rodata_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_G);
    }

    for (pp = data_start; pp < RAM_START + MEGA_SIZE; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Remaining RAM mapped in 2MB megapages

    for (pp = RAM_START + MEGA_SIZE; pp < RAM_END; pp += MEGA_SIZE) {
        main_pt1_0x80000[VPN1((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Enable paging; this part always makes me nervous.

    main_mtag = ptab_to_mtag(main_pt2, 0);
    csrw_satp(main_mtag);

    // Give the memory between the end of the kernel image and the next page
    // boundary to the heap allocator, but make sure it is at least
    // HEAP_INIT_MIN bytes.

    heap_start = _kimg_end;
    heap_end = (void *)ROUND_UP((uintptr_t)heap_start, PAGE_SIZE);

    if (heap_end - heap_start < HEAP_INIT_MIN) {
        heap_end += ROUND_UP(HEAP_INIT_MIN - (heap_end - heap_start), PAGE_SIZE);
    }

    if (RAM_END < heap_end) panic("out of memory");

    // Initialize heap memory manager

    heap_init(heap_start, heap_end);

    debug("Heap allocator: [%p,%p): %zu KB free", heap_start, heap_end,
          (heap_end - heap_start) / 1024);

    // we need to initialize the free chunk list with all memory from heap_end to RAM_END
    // Reserve 512 pages (2MB) at the end as safety margin to prevent any allocations
    // or memory operations from getting close enough to RAM_END that they could
    // write past the physical memory boundary
    free_chunk_list = (struct page_chunk *)heap_end;
    free_chunk_list->next = NULL;
    uint32_t total_pages = (RAM_END - heap_end) / PAGE_SIZE;
    free_chunk_list->pagecnt = (total_pages > 512) ? (total_pages - 512) : 0;

    // Allow supervisor to access user memory. We could be more precise by only
    // enabling supervisor access to user memory when we are explicitly trying
    // to access user memory, and disable it at other times. This would catch
    // bugs that cause inadvertent access to user memory (due to bugs).

    csrs_sstatus(RISCV_SSTATUS_SUM);

    memory_initialized = 1;
}

mtag_t active_mspace(void) { return active_space_mtag(); }

mtag_t switch_mspace(mtag_t mtag) {
    mtag_t prev;

    prev = csrrw_satp(mtag);
    sfence_vma();
    return prev;
}

mtag_t clone_active_mspace(void) {
    struct pte *current_ptab;
    struct pte *cloned_ptab;
    mtag_t new_mtag;
    trace("%s()", __func__);
    //we need to get the current page table
    current_ptab = active_space_ptab();
    //we need to clone the page table
    cloned_ptab = ptab_clone(current_ptab);
    if (cloned_ptab == NULL) {
    return (mtag_t)0;
    }
    //we need to create a memory tag for the cloned page table
    new_mtag = ptab_to_mtag(cloned_ptab, 0);
    return new_mtag;
}

void reset_active_mspace(void) {
    struct pte *ptab;
    trace("%s()", __func__);
    ptab = active_space_ptab();
    ptab_reset(ptab);
    sfence_vma();
}

mtag_t discard_active_mspace(void) {
    struct pte *ptab;
    trace("%s()", __func__);
    ptab = active_space_ptab();
    switch_mspace(main_mtag);
    if (ptab != main_pt2) {
        ptab_discard(ptab);
    }
    return main_mtag;
}

// The map_page() function maps a single page into the active address space at
// the specified address. The map_range() function maps a range of contiguous
// pages into the active address space. Note that map_page() is a special case
// of map_range(), so it can be implemented by calling map_range(). Or
// map_range() can be implemented by calling map_page() for each page in the
// range. The current implementation does the latter.

// We currently map 4K pages only. At some point it may be disirable to support
// mapping megapages and gigapages.

void *map_page(uintptr_t vma, void *pp, int rwxug_flags) {
    struct pte *ptab;
    unsigned long vpn;
    trace("%s(%p, %p, %d)", __func__, (void *)vma, pp, rwxug_flags);
    // we need to check alignment
    if (vma % PAGE_SIZE != 0) {
        panic("map_page: vma not page-aligned!!!");
    }
    ptab = active_space_ptab();
    vpn = VPN(vma);
    // we need to insert the page into the page table
    ptab_insert(ptab, vpn, pp, rwxug_flags);
    sfence_vma();
    return (void *)vma;
}

void *map_range(uintptr_t vma, size_t size, void *pp, int rwxug_flags) {
    size_t offset;
    unsigned long page_count;
    trace("%s(%p, %zu, %p, %d)", __func__, (void *)vma, size, pp, rwxug_flags);
    // we need to check alignment
    if (vma % PAGE_SIZE != 0) {
        panic("map_range: vma not page-aligned!!");
    }
    // we need to round up size to page boundary
    page_count = ROUND_UP(size, PAGE_SIZE) / PAGE_SIZE;
    // we need to map each page in the range
    for (offset = 0; offset < page_count; offset++) {
        map_page(vma + offset * PAGE_SIZE, (char *)pp + offset * PAGE_SIZE, rwxug_flags);
    }
    return (void *)vma;
}

void *alloc_and_map_range(uintptr_t vma, size_t size, int rwxug_flags) {
    void *pp;
    unsigned long page_count;
    trace("%s(%p, %zu, %d)", __func__, (void *)vma, size, rwxug_flags);
    // we need to round up size to page boundary
    page_count = ROUND_UP(size, PAGE_SIZE) / PAGE_SIZE;
    // we need to allocate and locate physical pages
    pp = alloc_phys_pages(page_count);
    // we need to map them to virtual address
    return map_range(vma, size, pp, rwxug_flags);
}

void set_range_flags(const void *vp, size_t size, int rwxug_flags) {
    struct pte *ptab;
    uintptr_t vma;
    unsigned long page_count;
    size_t offset;
    trace("%s(%p, %zu, %d)", __func__, vp, size, rwxug_flags);
    vma = (uintptr_t)vp;
    // we need to check alignment
    if (vma % PAGE_SIZE != 0) {
        panic("set_range_flags: vma not page-aligned!!!!");
    }
    // we need to get the active page table
    ptab = active_space_ptab();
    page_count = ROUND_UP(size, PAGE_SIZE) / PAGE_SIZE;
    // we need to adjust flags for each page in the range
    for (offset = 0; offset < page_count; offset++) {
        unsigned long vpn = VPN(vma + offset * PAGE_SIZE);
        ptab_adjust(ptab, vpn, rwxug_flags);
    }
    sfence_vma();
}

void unmap_and_free_range(void *vp, size_t size) {
    struct pte *ptab;
    uintptr_t vma;
    unsigned long page_count;
    size_t offset;
    trace("%s(%p, %zu)", __func__, vp, size);
    vma = (uintptr_t)vp;
    // we need to check alignment
    // Check alignment
    if (vma % PAGE_SIZE != 0) {
        panic("unmap_and_free_range: vma not page-aligned!");
    }
    // we need to get the active page table
    ptab = active_space_ptab();
    page_count = ROUND_UP(size, PAGE_SIZE) / PAGE_SIZE;
    // we need to unmap and free each page in the range
    for (offset = 0; offset < page_count; offset++) {
        unsigned long vpn = VPN(vma + offset * PAGE_SIZE);
        void *pp = ptab_remove(ptab, vpn);
        // we need to free the physical page
        if (pp != NULL) {
            free_phys_page(pp);
        }
    }
    sfence_vma();
}

int validate_vptr(const void *vp, size_t len, int rwxu_flags) {
    struct pte *ptab;
    uintptr_t vma;
    uintptr_t end_vma;
    uintptr_t current_vma;
    trace("%s(%p, %zu, %d)", __func__, vp, len, rwxu_flags);
    vma = (uintptr_t)vp;
    // we need to check if pointer is wellformed
    if (!wellformed(vma)) {
        return -EINVAL;
    }
    // we need to check for wraparound
    if (vma + len < vma) {
        return -EINVAL;
    }
    // we need to check length
    if (len == 0) {
        return 0;
    }
    // we need to get the active page table
    ptab = active_space_ptab();
    end_vma = vma + len;
    // we need to iterate through all pages in the range
    for (current_vma = ROUND_DOWN(vma, PAGE_SIZE); current_vma < end_vma; current_vma += PAGE_SIZE) {
        struct pte *pte;
        unsigned long vpn = VPN(current_vma);
        // we need to fetch the page table entry
        pte = ptab_fetch(ptab, vpn);
        // we need to check if page is mapped
        if (pte == NULL || !PTE_VALID(*pte)) {
            return -EACCESS;
        }
        // we need to check flags (note: we only check RWXU flags, not G)
        if ((pte->flags & rwxu_flags) != rwxu_flags) {
            return -EACCESS;
        }
    }
    return 0;
}

int validate_vstr(const char *vs, int rug_flags) {
    struct pte *ptab;
    uintptr_t vma;
    const char *current;
    trace("%s(%p, %d)", __func__, vs, rug_flags);
    vma = (uintptr_t)vs;
    // we need to check if pointer is wellformed
    if (!wellformed(vma)) {
        return -EINVAL;
    }
    // we need to get the active page table
    ptab = active_space_ptab();
    current = vs;
    // we need to iterate through string until null terminator
    while (1) {
        uintptr_t current_vma = (uintptr_t)current;
        uintptr_t page_vma = ROUND_DOWN(current_vma, PAGE_SIZE);
        struct pte *pte;
        unsigned long vpn = VPN(page_vma);

        pte = ptab_fetch(ptab, vpn);
        // we need to check if page is mapped
        if (pte == NULL || !PTE_VALID(*pte)) {
            return -EACCESS;
        }
        // we need to check flags (strings should be readable)
        if ((pte->flags & (PTE_R | rug_flags)) != (PTE_R | rug_flags)) {
            return -EACCESS;
        }
        // we need to scan through this page until we hit page boundary or null terminator
        uintptr_t page_end = page_vma + PAGE_SIZE;
        while ((uintptr_t)current < page_end) {
            if (*current == '\0') {
                return 0;
            }
            current++;
        }
    }
}

void *alloc_phys_page(void) {
    return alloc_phys_pages(1);
}

void free_phys_page(void *pp) {
    free_phys_pages(pp, 1);
}

void *alloc_phys_pages(unsigned int cnt) {
    struct page_chunk **best_prev = NULL;
    struct page_chunk **cur_prev = &free_chunk_list;
    struct page_chunk *cur = free_chunk_list;
    struct page_chunk *best = NULL;
    void *result;
    trace("%s(%u)", __func__, cnt);
    if (cnt == 0) return NULL;
    // we need to find the smallest chunk that fits (best-fit)
    while (cur != NULL) {
        if (cur->pagecnt >= cnt) {
            if (best == NULL || cur->pagecnt < best->pagecnt) {
                best = cur;
                best_prev = cur_prev;
            }
        }
        cur_prev = &cur->next;
        cur = cur->next;
    }
    if (best == NULL) {
        panic("out of physical memory");
    }
    // we need to remove chunk from list if exact match
    if (best->pagecnt == cnt) {
        *best_prev = best->next;
        result = (void *)best;
    } else {
        // we need to break off the needed portion from the end of the chunk
        result = (void *)((char *)best + (best->pagecnt - cnt) * PAGE_SIZE);
        best->pagecnt -= cnt;
    }
    // we need to zero out the allocated pages for security
    memset(result, 0, cnt * PAGE_SIZE);
    return result;
}

void free_phys_pages(void *pp, unsigned int cnt) {
    struct page_chunk *chunk;
    trace("%s(%p, %u)", __func__, pp, cnt);
    if (pp == NULL || cnt == 0) return;
    // we need to create a new chunk and add it to the front of the free list
    chunk = (struct page_chunk *)pp;
    chunk->pagecnt = cnt;
    chunk->next = free_chunk_list;
    free_chunk_list = chunk;
}

unsigned long free_phys_page_count(void) {
    unsigned long count = 0;
    struct page_chunk *cur = free_chunk_list;
    // we need to iterate through the free chunk list
    while (cur != NULL) {
        count += cur->pagecnt;
        cur = cur->next;
    }
    return count;
}

int handle_umode_page_fault(struct trap_frame *tfr, uintptr_t vma) {
    void *pp;
    uintptr_t page_vma;
    trace("%s(%p, %p)", __func__, tfr, (void *)vma);
    (void)tfr;  
    // we need to check if the address is wellformed
    if (!wellformed(vma)) {
        return 0;  
    }
    // we need to round down to page boundary
    page_vma = ROUND_DOWN(vma, PAGE_SIZE);
    // we need to check if page fault is within user memory space
    if (page_vma >= UMEM_START_VMA && page_vma < UMEM_END_VMA) {
        // we need to lazy allocate (demand paging) for user memory
        struct pte *ptab = active_space_ptab();
        struct pte *pte = ptab_fetch(ptab, VPN(page_vma));
        // we need to check if page is already mapped
        if (pte != NULL && PTE_VALID(*pte)) {
            return 0;  
        }
        // we need to allocate a new physical page
        pp = alloc_phys_page();
        if (pp == NULL) {
            return 0;  
        }
        // we need to map the page as user-accessible (readable, writable, user-mode)
        map_page(page_vma, pp, PTE_R | PTE_W | PTE_U);
        return 1;  
    }
    return 0;  
}

/**
 * @brief Reads satp to retrieve tag for active memory space
 * @return Tag for active memory space
 */
mtag_t active_space_mtag(void) { return csrr_satp(); }

/**
 * @brief Constructs tag from page table address and address space identifier
 * @param ptab Pointer to page table to use in tag
 * @param asid Address space identifier to use in tag
 * @return Memory tag formed from paging mode, page table address, and ASID
 */
static inline mtag_t ptab_to_mtag(struct pte *ptab, unsigned int asid) {
    return (((unsigned long)PAGING_MODE << RISCV_SATP_MODE_shift) |
            ((unsigned long)asid << RISCV_SATP_ASID_shift) | pagenum(ptab) << RISCV_SATP_PPN_shift);
}

/**
 * @brief Retrives a page table address from a tag
 * @param mtag Tag to extract page table address from
 * @return Pointer to page table retrieved from tag
 */
static inline struct pte *mtag_to_ptab(mtag_t mtag) { return (struct pte *)((mtag << 20) >> 8); }

/**
 * @brief Returns the address of the page table corresponding to the active memory space
 * @return Pointer to page table extracted from active memory space tag
 */
static inline struct pte *active_space_ptab(void) { return mtag_to_ptab(active_space_mtag()); }

/**
 * @brief Constructs a physical pointer from a physical page number
 * @param n Physical page number to derive physical pointer from
 * @return Pointer to memory corresponding to physical page
 */
static inline void *pageptr(uintptr_t n) { return (void *)(n << PAGE_ORDER); }

/**
 * @brief Constructs a physical page number from a pointer
 * @param p Pointer to derive physical page number from
 * @return Physical page number corresponding to pointer
 */
static inline unsigned long pagenum(const void *p) { return (unsigned long)p >> PAGE_ORDER; }

/**
 * @brief Checks if bits 63:38 of passed virtual memory address are all 1 or all 0
 * @param vma Virtual memory address to check well-formedness of
 * @return 1 if pointer is well-formed, 0 otherwise
 */
static inline int wellformed(uintptr_t vma) {
    // Address bits 63:38 must be all 0 or all 1
    uintptr_t const bits = (intptr_t)vma >> 38;
    return (!bits || !(bits + 1));
}

/**
 * @brief Constructs a page table entry corresponding to a leaf
 * @details For our purposes, a leaf PTE has the A, D, and V flags set
 * @param pp Physical address to set physical page number of PTE from
 * @param rwxug_flags Flags to set on PTE
 * @return PTE initialized with proper flags and PPN
 */
static inline struct pte leaf_pte(const void *pp, uint_fast8_t rwxug_flags) {
    return (struct pte){.flags = rwxug_flags | PTE_A | PTE_D | PTE_V, .ppn = pagenum(pp)};
}

/**
 * @brief Constructs a page table entry corresponding to a page table
 * @param pt Physical address to set physical page number of PTE from
 * @param g_flag Flags to set on PTE (should either be G flag or nothing)
 * @return PTE initialized with proper flags and PPN
 */
static inline struct pte ptab_pte(const struct pte *pt, uint_fast8_t g_flag) {
    return (struct pte){.flags = g_flag | PTE_V, .ppn = pagenum(pt)};
}

/**
 * @brief Returns an empty pte
 * @return An empty pte
 */
static inline struct pte null_pte(void) { return (struct pte){}; }
