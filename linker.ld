/*
 * Nyota OS — Linker Script
 * Places the kernel binary at physical address 0x10000.
 * The bootloader loads the raw disk sectors there and jumps directly to _start.
 */

ENTRY(_start)

SECTIONS {
    /* Load address: physical 0x10000 (64 KB mark) */
    . = 0x10000;

    /* Executable code */
    .text ALIGN(16) : {
        *(.text)
    }

    /* Read-only data (string literals, const tables) */
    .rodata ALIGN(16) : {
        *(.rodata)
        *(.rodata.*)
    }

    /* Initialised data */
    .data ALIGN(16) : {
        *(.data)
    }

    /* Zero-initialised data */
    .bss ALIGN(16) : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }
}
