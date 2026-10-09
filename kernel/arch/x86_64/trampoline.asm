; =============================================================================
; Nyota OS — Application Processor (AP) Boot Trampoline (Phase 11 SMP)
; Executed by APs when receiving Startup IPI (SIPI).
; Target: 16-bit Real Mode (0x8000) -> 32-bit Protected Mode -> 64-bit Long Mode
; =============================================================================

[org 0x8000]
[bits 16]

trampoline_start:
    cli
    cld

    ; Normalize segment registers
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Load temporary 32-bit GDT
    lgdt [0x8000 + (gdt32_desc - trampoline_start)]

    ; Enable Protected Mode (PE bit in CR0)
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to 32-bit protected mode
    jmp dword 0x08:(0x8000 + (.pmode - trampoline_start))

; ── 32-bit Protected Mode ────────────────────────────────────────────────────
[bits 32]
.pmode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set CR3 to kernel PML4 (0x1000)
    mov eax, 0x1000
    mov cr3, eax

    ; Enable Physical Address Extension (PAE, bit 5 of CR4)
    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    ; Enable Long Mode (LME, bit 8 of EFER MSR 0xC0000080)
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr

    ; Enable Paging (PG, bit 31 of CR0)
    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax

    ; Far jump into 64-bit Long Mode
    jmp 0x18:(0x8000 + (.lmode - trampoline_start))

; ── 64-bit Long Mode ─────────────────────────────────────────────────────────
[bits 64]
.lmode:
    ; Reload 64-bit data segments
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Read mailbox parameters set by BSP:
    ; 0x8F00: Target kernel stack (uint64_t)
    ; 0x8F08: Target C entry point (uint64_t)
    ; 0x8F10: Logical CPU ID (uint32_t)
    mov rsp, [0x8F00]
    mov edi, [0x8F10]    ; RDI = cpu_id (first parameter in System V AMD64 ABI)
    mov rax, [0x8F08]    ; RAX = ap_kernel_entry
    call rax

.hang:
    cli
    hlt
    jmp .hang

; ── Temporary GDT for Mode Transitions ───────────────────────────────────────
align 16
gdt32:
    dq 0x0000000000000000       ; 0x00: Null Descriptor
    dq 0x00CF9A000000FFFF       ; 0x08: 32-bit Code (DPL 0, 4GB)
    dq 0x00CF92000000FFFF       ; 0x10: 32-bit Data (DPL 0, 4GB)
    dq 0x00AF9A000000FFFF       ; 0x18: 64-bit Code (DPL 0, L=1)
    dq 0x00AF92000000FFFF       ; 0x20: 64-bit Data (DPL 0)
gdt32_end:

gdt32_desc:
    dw (gdt32_end - gdt32 - 1)
    dd (0x8000 + (gdt32 - trampoline_start))

trampoline_end:
