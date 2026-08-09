; =============================================================================
; Nyota OS — Stage 1 Bootloader
; Target: x86 Real Mode (16-bit), loaded by BIOS at 0x7C00
; Task:   Print message, load kernel from disk, switch to 32-bit protected mode
; =============================================================================

[org 0x7C00]
[bits 16]

; ── Constants ────────────────────────────────────────────────────────────────
KERNEL_SEGMENT  equ 0x1000      ; Kernel physical load address: 0x1000 * 16 = 0x10000
NUM_SECTORS     equ 50          ; Sectors to read (50 * 512 = 25 KB max kernel)

; ── Entry Point ──────────────────────────────────────────────────────────────
start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  sp, 0x7BFF             ; Stack grows down, just below bootloader
    sti

    mov  [boot_drive], dl       ; BIOS puts boot drive in DL

    mov  si, msg_loading
    call print16

    call load_kernel

    cli                         ; Disable interrupts for mode switch
    lgdt [gdt_descriptor]       ; Load GDT

    ; Set CR0.PE = 1 (enable protected mode)
    mov  eax, cr0
    or   eax, 0x00000001
    mov  cr0, eax

    ; Far jump: flush pipeline, set CS to CODE_SEG, enter 32-bit mode
    jmp  CODE_SEG:pm_start

; ── 16-bit helpers ───────────────────────────────────────────────────────────
print16:
    push ax
    mov  ah, 0x0E               ; BIOS teletype output
.loop:
    lodsb
    test al, al
    jz   .done
    int  0x10
    jmp  .loop
.done:
    pop  ax
    ret

load_kernel:
    pusha
    mov  ah, 0x02               ; Function: read sectors
    mov  al, NUM_SECTORS        ; Number of sectors
    mov  ch, 0                  ; Cylinder 0
    mov  dh, 0                  ; Head 0
    mov  cl, 2                  ; Start at sector 2 (sector after MBR)
    mov  dl, [boot_drive]

    mov  bx, KERNEL_SEGMENT
    mov  es, bx                 ; ES = 0x1000
    xor  bx, bx                 ; BX = 0x0000  →  ES:BX = physical 0x10000
    int  0x13                   ; BIOS disk read
    jc   .disk_error
    popa
    ret
.disk_error:
    mov  si, msg_disk_error
    call print16
    jmp  $                      ; Hang

; ── Global Descriptor Table (GDT) ────────────────────────────────────────────
gdt_start:
    ; Descriptor 0: Null
    dq 0x0000000000000000

    ; Descriptor 1: Kernel Code — Base=0, Limit=4GB, Ring 0, Execute/Read
gdt_code:
    dw 0xFFFF                   ; Limit [15:0]
    dw 0x0000                   ; Base  [15:0]
    db 0x00                     ; Base  [23:16]
    db 10011010b                ; Access: P=1, DPL=00, S=1, Type=1010 (code, read)
    db 11001111b                ; Flags: G=1,D=1,L=0,AVL=0 | Limit [19:16]=0xF
    db 0x00                     ; Base  [31:24]

    ; Descriptor 2: Kernel Data — Base=0, Limit=4GB, Ring 0, Read/Write
gdt_data:
    dw 0xFFFF                   ; Limit [15:0]
    dw 0x0000                   ; Base  [15:0]
    db 0x00                     ; Base  [23:16]
    db 10010010b                ; Access: P=1, DPL=00, S=1, Type=0010 (data, write)
    db 11001111b                ; Flags: G=1,D=1,L=0,AVL=0 | Limit [19:16]=0xF
    db 0x00                     ; Base  [31:24]
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; GDT limit = size - 1
    dd gdt_start                ; GDT base (physical address)

CODE_SEG equ gdt_code - gdt_start   ; = 0x08
DATA_SEG equ gdt_data - gdt_start   ; = 0x10

; ── 32-bit Protected Mode Initialisation ─────────────────────────────────────
[bits 32]
pm_start:
    ; Point all data segments at the kernel data descriptor
    mov  ax, DATA_SEG
    mov  ds, ax
    mov  ss, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax

    ; Set up a proper stack well above the kernel
    mov  ebp, 0x00090000
    mov  esp, ebp

    ; Hand off to the kernel
    jmp  0x10000

; ── Strings (assembled as data, position doesn't affect earlier code) ─────────
[bits 16]
msg_loading:    db "Nyota OS: Loading kernel...", 0x0D, 0x0A, 0
msg_disk_error: db "ERROR: Disk read failed! Halting.", 0x0D, 0x0A, 0
boot_drive:     db 0

; ── Pad to 510 bytes and append boot signature ───────────────────────────────
times 510 - ($ - $$) db 0
dw 0xAA55
