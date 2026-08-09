; =============================================================================
; Nyota OS — GDT Flush
; Loads the new GDT and reloads all segment registers so they reference the
; descriptors we set up in gdt.c instead of the bootloader's GDT.
; =============================================================================

[bits 32]
global gdt_flush

; void gdt_flush(uint32_t gdt_ptr_addr);
gdt_flush:
    mov  eax, [esp + 4]     ; First argument: pointer to gdt_ptr struct
    lgdt [eax]              ; Load GDT register

    ; Reload data segment registers with kernel data selector (index 2 = 0x10)
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax

    ; Far jump to reload CS with kernel code selector (index 1 = 0x08)
    ; This flushes the prefetch queue and ensures we run with the new GDT.
    jmp  0x08:.flush
.flush:
    ret
