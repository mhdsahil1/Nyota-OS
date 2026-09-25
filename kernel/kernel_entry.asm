; =============================================================================
; Nyota OS — Kernel Entry Point (64-bit Long Mode)
; Linked at physical address 0x100000 (1 MB mark).
; First instruction executed inside the Nyota Kernel.
; =============================================================================

[bits 64]
default rel

global kernel_entry
extern kernel_main

section .text
kernel_entry:
    ; 1. Establish a clean, 16-byte aligned 64-bit stack frame
    mov  rsp, 0x90000
    xor  rbp, rbp

    ; 2. Clear Direction Flag (DF) as required by System V AMD64 ABI
    cld

    ; 3. Clear/zero general-purpose registers
    xor  rax, rax
    xor  rbx, rbx
    xor  rcx, rcx
    xor  rdx, rdx
    xor  rsi, rsi
    xor  rdi, rdi
    xor  r8,  r8
    xor  r9,  r9
    xor  r10, r10
    xor  r11, r11
    xor  r12, r12
    xor  r13, r13
    xor  r14, r14
    xor  r15, r15

    ; 4. Call C kernel entry point: void kernel_main(void)
    call kernel_main

    ; 5. Safety hang loop if kernel_main ever returns
.hang:
    cli
    hlt
    jmp  .hang
