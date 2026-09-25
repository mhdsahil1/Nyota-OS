; =============================================================================
; Nyota OS — GDT Flush Routine (64-bit)
; Loads the GDTR and reloads all segment registers.
; =============================================================================

[bits 64]
default rel
global gdt_flush

section .text
gdt_flush:
    ; System V ABI passes pointer in RDI; Windows ABI passes in RCX.
    test rdi, rdi
    jnz  .use_rdi
    mov  rdi, rcx
.use_rdi:
    lgdt [rdi]

    ; Reload data segment selectors with kernel data descriptor (0x10)
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax

    ; Reload CS by pushing 64-bit selector + target address and doing retfq
    push qword 0x08
    lea  rax, [.flush_cs]
    push rax
    retfq

.flush_cs:
    ret
