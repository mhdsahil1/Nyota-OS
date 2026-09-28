/* =============================================================================
 * Nyota OS — ELF64 Binary Format Header (Phase 6)
 * Standards-compliant 64-bit ELF executable definitions and loader interfaces.
 * =========================================================================== */

#ifndef NYOTA_ELF_ELF_H
#define NYOTA_ELF_ELF_H

#include "types.h"
#include "paging.h"
#include "fs/nyotafs.h"

/* ELF Identification Indices */
#define EI_MAG0         0
#define EI_MAG1         1
#define EI_MAG2         2
#define EI_MAG3         3
#define EI_CLASS        4
#define EI_DATA         5
#define EI_VERSION      6
#define EI_OSABI        7
#define EI_ABIVERSION   8
#define EI_PAD          9
#define EI_NIDENT       16

/* Magic bytes */
#define ELFMAG0         0x7F
#define ELFMAG1         'E'
#define ELFMAG2         'L'
#define ELFMAG3         'F'

/* Class */
#define ELFCLASSNONE    0
#define ELFCLASS32      1
#define ELFCLASS64      2

/* Data encoding */
#define ELFDATANONE     0
#define ELFDATA2LSB     1   /* Little endian */
#define ELFDATA2MSB     2   /* Big endian */

/* Version */
#define EV_CURRENT      1

/* Types */
#define ET_NONE         0
#define ET_REL          1
#define ET_EXEC         2
#define ET_DYN          3
#define ET_CORE         4

/* Machine */
#define EM_NONE         0
#define EM_X86_64       62  /* AMD x86-64 architecture */

/* Program Header Types */
#define PT_NULL         0
#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_NOTE         4
#define PT_SHLIB        5
#define PT_PHDR         6
#define PT_GNU_STACK    0x6474e551

/* Program Header Flags */
#define PF_X            0x1  /* Execute */
#define PF_W            0x2  /* Write */
#define PF_R            0x4  /* Read */

#pragma pack(push, 1)

/* ELF64 File Header */
typedef struct {
    uint8_t  e_ident[EI_NIDENT];
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
} Elf64_Ehdr;

/* ELF64 Program Header */
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

#pragma pack(pop)

/* ELF Loader APIs */
bool elf_validate_header(const Elf64_Ehdr *ehdr);
int elf_load_executable(nyota_fs_t *fs, uint64_t inode_num, page_table_t *pml4, uint64_t *out_entry);

#endif /* NYOTA_ELF_ELF_H */
