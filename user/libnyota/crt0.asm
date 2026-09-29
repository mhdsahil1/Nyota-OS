; =============================================================================
; Nyota OS — Userspace C Runtime Startup (crt0.asm)
; Entry point for all Ring 3 ELF executables.
; =============================================================================

[BITS 64]

global _start
global __main
global environ
extern main
extern exit

default rel

section .data
align 8
environ:
    dq 0

section .text

align 16
_start:
    ; System V AMD64 ABI:
    ; RDI = argc
    ; RSI = argv
    ; RDX = envp
    mov [rel environ], rdx

    call main

    ; Pass main()'s return code as exit status
    mov rdi, rax
    call exit

    ; Catch unexpected return from exit
.hang:
    hlt
    jmp .hang

align 16
__main:
    ret
