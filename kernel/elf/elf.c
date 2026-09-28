/* =============================================================================
 * Nyota OS — ELF64 Binary Loader Implementation (Phase 6)
 * Validates ELF64 structures, maps PT_LOAD segments with permissions,
 * and sets up executable entry points.
 * =========================================================================== */

#include "elf/elf.h"
#include "fs/vfs.h"
#include "pmm.h"
#include "paging.h"
#include "memory.h"
#include "vga.h"
#include "kernel.h"
#include "process.h"

bool elf_validate_header(const Elf64_Ehdr *ehdr) {
    if (!ehdr) return false;

    /* Magic check */
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        return false;
    }

    /* Class: 64-bit */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        return false;
    }

    /* Data: 2's complement, little endian */
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        return false;
    }

    /* Version */
    if (ehdr->e_ident[EI_VERSION] != EV_CURRENT) {
        return false;
    }

    /* Machine: AMD x86-64 */
    if (ehdr->e_machine != EM_X86_64) {
        return false;
    }

    /* Program headers check */
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0 || ehdr->e_phnum > 64) {
        return false;
    }

    return true;
}

int elf_load_executable(nyota_fs_t *fs, uint64_t inode_num, page_table_t *pml4, uint64_t *out_entry) {
    if (!fs || !pml4 || !out_entry) return NYOTA_EINVAL;

    nyota_inode_t inode;
    if (nyotafs_read_inode(fs, inode_num, &inode) != 0) {
        return NYOTA_ENOENT;
    }

    if (inode.size < sizeof(Elf64_Ehdr)) {
        return NYOTA_ENOEXEC;
    }

    /* 1. Read ELF Header */
    Elf64_Ehdr ehdr;
    if (nyotafs_read_file(fs, &inode, 0, &ehdr, sizeof(Elf64_Ehdr)) != sizeof(Elf64_Ehdr)) {
        return NYOTA_EIO;
    }

    /* 2. Validate ELF Header */
    if (!elf_validate_header(&ehdr)) {
        return NYOTA_ENOEXEC;
    }

    /* 3. Read and Process Program Headers */
    bool has_loadable_segment = false;

    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr phdr;
        uint64_t ph_offset = ehdr.e_phoff + (uint64_t)i * ehdr.e_phentsize;

        if (nyotafs_read_file(fs, &inode, ph_offset, &phdr, sizeof(Elf64_Phdr)) != sizeof(Elf64_Phdr)) {
            return NYOTA_EIO;
        }

        if (phdr.p_type != PT_LOAD) {
            continue;
        }

        if (phdr.p_memsz == 0) {
            continue;
        }

        if (phdr.p_filesz > phdr.p_memsz) {
            return NYOTA_ENOEXEC;
        }

        /* Verify virtual address is within valid user space */
        if (phdr.p_vaddr < USER_SPACE_BASE || phdr.p_vaddr >= USER_SPACE_END) {
            return NYOTA_ENOEXEC;
        }
        if (phdr.p_vaddr + phdr.p_memsz > USER_SPACE_END || phdr.p_vaddr + phdr.p_memsz < phdr.p_vaddr) {
            return NYOTA_ENOEXEC;
        }

        has_loadable_segment = true;

        /* Determine page permission flags */
        uint64_t page_flags = PAGE_PRESENT | PAGE_USER;
        if (phdr.p_flags & PF_W) {
            page_flags |= PAGE_WRITABLE;
        }

        uint64_t vstart = PAGE_ALIGN_DOWN(phdr.p_vaddr);
        uint64_t vend = PAGE_ALIGN_UP(phdr.p_vaddr + phdr.p_memsz);

        /* Map and allocate physical memory frames */
        for (uint64_t page = vstart; page < vend; page += PAGE_SIZE) {
            uint64_t phys = paging_get_physical_in(pml4, page);
            if (phys == 0) {
                void *pframe = pmm_alloc_page();
                if (!pframe) {
                    return NYOTA_ENOMEM;
                }
                memset(pframe, 0, PAGE_SIZE);
                if (!paging_map_page_in(pml4, page, (uint64_t)pframe, page_flags)) {
                    pmm_free_page(pframe);
                    return NYOTA_ENOMEM;
                }
            } else if (phdr.p_flags & PF_W) {
                /* Update flags if segment requires write */
                paging_map_page_in(pml4, page, phys, page_flags | PAGE_WRITABLE);
            }
        }

        /* Copy file content into allocated virtual pages */
        size_t bytes_to_copy = (size_t)phdr.p_filesz;
        uint64_t src_offset = phdr.p_offset;
        uint64_t dst_vaddr = phdr.p_vaddr;

        while (bytes_to_copy > 0) {
            uint64_t page_base = PAGE_ALIGN_DOWN(dst_vaddr);
            uint32_t page_offset = (uint32_t)(dst_vaddr - page_base);
            size_t chunk = PAGE_SIZE - page_offset;
            if (chunk > bytes_to_copy) {
                chunk = bytes_to_copy;
            }

            uint64_t phys_addr = paging_get_physical_in(pml4, page_base);
            if (phys_addr == 0) {
                return NYOTA_ENOMEM;
            }

            void *target_ptr = (void *)(phys_addr + page_offset);
            if (nyotafs_read_file(fs, &inode, src_offset, target_ptr, chunk) != (int)chunk) {
                return NYOTA_EIO;
            }

            src_offset += chunk;
            dst_vaddr += chunk;
            bytes_to_copy -= chunk;
        }

        /* Note: BSS is already zeroed because newly allocated pages are zeroed with memset */
    }

    if (!has_loadable_segment) {
        return NYOTA_ENOEXEC;
    }

    /* Validate entry point */
    if (ehdr.e_entry < USER_SPACE_BASE || ehdr.e_entry >= USER_SPACE_END) {
        return NYOTA_ENOEXEC;
    }

    *out_entry = ehdr.e_entry;
    return NYOTA_OK;
}
