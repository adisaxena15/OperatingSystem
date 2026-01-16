
/*! @file elf.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief ELF file loader
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA

*/

#ifdef ELF_TRACE
#define TRACE
#endif

#ifdef ELF_DEBUG
#define DEBUG
#endif

#include "elf.h"

#include <stdint.h>

#include "conf.h"
#include "console.h"
#include "error.h"
#include "memory.h"
#include "misc.h"
#include "string.h"
#include "uio.h"

// Offsets into e_ident

#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define EI_OSABI 7
#define EI_ABIVERSION 8
#define EI_PAD 9

// ELF header e_ident[EI_CLASS] values

#define ELFCLASSNONE 0
#define ELFCLASS32 1
#define ELFCLASS64 2

// ELF header e_ident[EI_DATA] values

#define ELFDATANONE 0
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

// ELF header e_ident[EI_VERSION] values

#define EV_NONE 0
#define EV_CURRENT 1

// ELF header e_type values

enum elf_et { ET_NONE = 0, ET_REL, ET_EXEC, ET_DYN, ET_CORE };

/*! @struct elf64_ehdr
    @brief ELF header struct
*/
struct elf64_ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

/*! @enum elf_pt
    @brief Program header p_type values
*/
enum elf_pt { PT_NULL = 0, PT_LOAD, PT_DYNAMIC, PT_INTERP, PT_NOTE, PT_SHLIB, PT_PHDR, PT_TLS };

// Program header p_flags bits

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

/*! @struct elf64_phdr
    @brief Program header struct
*/
struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

// ELF header e_machine values (short list)

#define EM_RISCV 243
/**
 * \brief Validates and loads an ELF file into memory.
 *
 * This function validates an ELF file, then loads its contents into memory,
 * returning the start of the entry point through \p eptr.
 *
 * The loader processes only program header entries of type `PT_LOAD`. The layouts
 * of structures and magic values can be found in the Linux ELF header file
 * `<uapi/linux/elf.h>`
 * The implementation should ensure that all loaded sections of the program are
 * mapped within the memory range `0x80100000` to `0x81000000`.
 *
 * Let's do some reading! The following documentation will be very helpful!
 * [Helpful doc](https://linux.die.net/man/5/elf)
 * Good luck!
 * [Educational video](https://www.youtube.com/watch?v=dQw4w9WgXcQ)
 *
 * \param[in]  uio  Pointer to an user I/O corresponding to the ELF file.
 * \param[out] eptr   Double pointer used to return the ELF file's entry point.
 *
 * \return 0 on success, or a negative error code on failure.
 */
int elf_load(struct uio* uio, void (**eptr)(void)) {
    struct elf64_ehdr ehdr;
    struct elf64_phdr phdr;
    long bytes_read;
    int result;
    // we now validate input parameters
    if (uio == NULL || eptr == NULL) {
        return -EINVAL;
    }
    // we now set the memory range constraints for user programs
    const uint64_t MIN_ADDR = UMEM_START_VMA;
    const uint64_t MAX_ADDR = UMEM_END_VMA;
    // we now step 1: Read and validate ELF header
    // we now set the position to the beginning of the file
    unsigned long long pos = 0;
    result = uio_cntl(uio, FCNTL_SETPOS, &pos);
    if (result < 0) {
        return result;
    }
    // we now read the ELF header
    bytes_read = uio_read(uio, &ehdr, sizeof(ehdr));
    if (bytes_read < 0) {
        return (int)bytes_read;
    }
    if (bytes_read < (long)sizeof(ehdr)) {
        return -EINVAL;
    }
    // we now validate the ELF magic number
    if (ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E' || 
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        return -EINVAL;
    }
    // we now validate the ELF class (must be 64-bit)
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        return -EINVAL;
    }
    // we now validate the data encoding (must be little-endian)
    if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
        return -EINVAL;
    }
    // we now validate the ELF version
    if (ehdr.e_ident[EI_VERSION] != EV_CURRENT || ehdr.e_version != EV_CURRENT) {
        return -EINVAL;
    }
    // we now validate the ELF type (must be executable)
    if (ehdr.e_type != ET_EXEC) {
        return -EINVAL;
    }
    // we now validate the machine type (must be RISC-V)
    if (ehdr.e_machine != EM_RISCV) {
        return -EINVAL;
    }
    // we now validate the ELF header size
    if (ehdr.e_ehsize != sizeof(struct elf64_ehdr)) {
        return -EINVAL;
    }
    // we now validate the entry point is in valid range
    if (ehdr.e_entry < MIN_ADDR || ehdr.e_entry >= MAX_ADDR) {
        return -EINVAL;
    }
    // we now validate program header entry size
    if (ehdr.e_phentsize != sizeof(struct elf64_phdr)) {
        return -EINVAL;
    }
    // we now validate program header offset if we have program headers
    if (ehdr.e_phnum > 0 && ehdr.e_phoff == 0) {
        return -EINVAL;
    }
    // we now track if we loaded at least one PT_LOAD segment
    int loaded_segment = 0;
    // we now step 2: Process program headers
    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        // we now seek to the program header
        pos = ehdr.e_phoff + (uint64_t)i * ehdr.e_phentsize;
        // we now check for overflow in program header offset calculation
        if (pos < ehdr.e_phoff) {
            return -EINVAL;
        }
        result = uio_cntl(uio, FCNTL_SETPOS, &pos);
        if (result < 0) {
            return result;
        }
        // we now read the program header
        bytes_read = uio_read(uio, &phdr, sizeof(phdr));
        if (bytes_read < 0) {
            return (int)bytes_read;
        }
        if (bytes_read < (long)sizeof(phdr)) {
            return -EINVAL;
        }
        // we now only process PT_LOAD segments
        if (phdr.p_type != PT_LOAD) {
            continue;
        }
        // we now skip empty segments
        if (phdr.p_memsz == 0) {
            continue;
        }
        // we now validate that file size does not exceed memory size
        if (phdr.p_filesz > phdr.p_memsz) {
            return -EINVAL;
        }
        // we now check for overflow in p_offset + p_filesz
        if (phdr.p_filesz > 0) {
            if (phdr.p_offset + phdr.p_filesz < phdr.p_offset) {
                return -EINVAL;
            }
        }
        // we now validate the address range
        if (phdr.p_vaddr < MIN_ADDR || phdr.p_vaddr >= MAX_ADDR) {
            return -EINVAL;
        }
        // we now check for overflow in p_vaddr + p_memsz FIRST before bounds check
        if (phdr.p_vaddr + phdr.p_memsz < phdr.p_vaddr) {
            return -EINVAL;
        }
        // we now validate the end address is within range
        if (phdr.p_vaddr + phdr.p_memsz > MAX_ADDR) {
            return -EINVAL;
        }
        // we now step 3: Compute mapping range
        uint64_t seg_start = phdr.p_vaddr;
        uint64_t seg_end = phdr.p_vaddr + phdr.p_memsz;
        // we now round to page boundaries for mapping
        uintptr_t map_start = (uintptr_t)ROUND_DOWN(seg_start, PAGE_SIZE);
        uintptr_t map_end = (uintptr_t)ROUND_UP(seg_end, PAGE_SIZE);
        size_t map_size = map_end - map_start;
        // we now validate mapping range
        if (map_start < MIN_ADDR || map_end > MAX_ADDR) {
            return -EINVAL;
        }
        // we now determine final page flags based on segment permissions
        int final_flags = PTE_U;  // User mode accessible
        if (phdr.p_flags & PF_R) final_flags |= PTE_R;
        if (phdr.p_flags & PF_W) final_flags |= PTE_W;
        if (phdr.p_flags & PF_X) final_flags |= PTE_X;
        // we now map as writable initially so kernel can load data, will fix permissions later
        int load_flags = PTE_U | PTE_R | PTE_W;
        // we now allocate and map pages for this segment
        void* mapped = alloc_and_map_range(map_start, map_size, load_flags);
        if (mapped == NULL) {
            return -ENOMEM;
        }
        // we now compute pointer to first byte of segment (may be inside first page)
        uint8_t* seg_virt = (uint8_t*)map_start + (seg_start - map_start);
        // we now step 4: Load segment data from file
        if (phdr.p_filesz > 0) {
            // we now seek to the file offset
            pos = phdr.p_offset;
            result = uio_cntl(uio, FCNTL_SETPOS, &pos);
            if (result < 0) {
                return result;
            }
            // we now read the segment data into the mapped virtual memory
            bytes_read = uio_read(uio, seg_virt, phdr.p_filesz);
            if (bytes_read < 0) {
                return (int)bytes_read;
            }
            if (bytes_read < (long)phdr.p_filesz) {
                return -EINVAL;
            }
        }
        // we now step 5: Zero out BSS section (if p_memsz > p_filesz)  
        if (phdr.p_memsz > phdr.p_filesz) {
            uint8_t* bss_start = seg_virt + phdr.p_filesz;
            size_t bss_size = phdr.p_memsz - phdr.p_filesz;
            memset(bss_start, 0, bss_size);
        }
        // we now step 6: Set final permissions on full mapped page range
        set_range_flags((const void*)map_start, map_size, final_flags);
        // we now mark that we successfully loaded a segment
        loaded_segment = 1;
    }
    // we now verify that we loaded at least one segment
    if (!loaded_segment) {
        return -EINVAL;
    }
    // we now step 5: Set entry point   
    *eptr = (void (*)(void))ehdr.e_entry;
    return 0;
}
