/* =============================================================================
 * Nyota OS — User Executable Linker Script (Phase 6)
 * Target: x86_64 Ring 3 User Mode (Isolated PML4[1] User Space)
 * Virtual Load Address: 0x0000008000001000 (512 GB window)
 * =========================================================================== */

ENTRY(_start)

SECTIONS
{
    . = 0x0000008000001000;

    .text ALIGN(0x1000) : {
        *(.text)
        *(.text.*)
    }

    .rodata ALIGN(0x1000) : {
        *(.rodata)
        *(.rodata.*)
        *(.rdata)
        *(.rdata.*)
    }

    .data ALIGN(0x1000) : {
        *(.data)
        *(.data.*)
    }

    .bss ALIGN(0x1000) : {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        _bss_end = .;
    }

    /DISCARD/ : {
        *(.pdata)
        *(.xdata)
        *(.idata*)
        *(.comment)
        *(.note*)
        *(.reloc)
    }
}
