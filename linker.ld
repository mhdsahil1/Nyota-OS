/* =============================================================================
 * Nyota OS — 64-bit Kernel Linker Script
 * Target: x86_64 Long Mode
 * Physical Load Address: 0x100000 (1 MB mark)
 * =========================================================================== */

ENTRY(kernel_entry)

SECTIONS
{
    /* Physical load address at 1MB mark */
    . = 0x100000;
    _kernel_start = .;

    /* Executable code */
    .text ALIGN(16) : {
        *(.text)
        *(.text.*)
    }

    /* Read-only constants, string literals */
    .rodata ALIGN(16) : {
        *(.rodata)
        *(.rodata.*)
        *(.rdata)
        *(.rdata.*)
    }

    /* Initialized data */
    .data ALIGN(16) : {
        *(.data)
        *(.data.*)
    }

    /* Zero-initialized data */
    .bss ALIGN(16) : {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        _bss_end = .;
    }

    _kernel_end = .;

    /* Discard unused or platform-specific metadata sections */
    /DISCARD/ : {
        *(.pdata)
        *(.xdata)
        *(.idata*)
        *(.comment)
        *(.note*)
    }
}
