; =============================================================================
; Nyota OS — Kernel Entry Point
; Linked at 0x10000 — first code the CPU executes after the bootloader hands off.
; Sets up a C-compatible stack frame and calls kernel_main().
; =============================================================================

[bits 32]
[extern kernel_main]
global _start

_start:
    ; Stack is already set up by bootloader (ESP = 0x90000)
    ; Call the C kernel — this should never return
    call  kernel_main

    ; Safety net: if kernel_main somehow returns, halt forever
.hang:
    cli
    hlt
    jmp  .hang
