; =============================================================================
; Nyota OS — Stage 1 Boot Sector (MBR)
; Target: x86 Real Mode (16-bit), loaded by BIOS at 0x0000:0x7C00
; Responsibilities:
;   - Initialize CPU segments and stack
;   - Store BIOS boot drive ID
;   - Load Stage 2 from disk into memory at 0x0000:0x8000
;   - Verify BIOS boot and transfer control to Stage 2
; =============================================================================

[org 0x7C00]
[bits 16]

STAGE2_LOAD_SEG  equ 0x0000
STAGE2_LOAD_OFF  equ 0x8000
STAGE2_SECTORS   equ 4          ; Read 4 sectors (2048 bytes) for Stage 2
STAGE2_START_SEC equ 2          ; 1-based sector 2 (LBA 1, right after MBR)

start:
    ; Disable interrupts during segment & stack initialization
    cli
    cld

    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  sp, 0x7C00             ; Stack grows downwards from 0x7C00
    sti

    ; BIOS delivers boot drive number in DL
    mov  [boot_drive], dl

    ; Display Stage 1 startup message
    mov  si, msg_stage1
    call print_string

    ; Load Stage 2 from disk
    mov  di, 3                  ; 3 retry attempts
.retry_load:
    mov  ah, 0x02               ; BIOS read sectors
    mov  al, STAGE2_SECTORS     ; Number of sectors
    mov  ch, 0                  ; Cylinder 0
    mov  cl, STAGE2_START_SEC   ; Sector 2
    mov  dh, 0                  ; Head 0
    mov  dl, [boot_drive]       ; Drive number
    mov  bx, STAGE2_LOAD_OFF    ; Buffer offset (ES:BX = 0x0000:0x8000)
    int  0x13
    jnc  .load_success

    ; Reset disk controller before retrying
    xor  ax, ax
    mov  dl, [boot_drive]
    int  0x13

    dec  di
    jnz  .retry_load

    ; If we get here, disk read failed permanently
    mov  si, msg_disk_err
    call print_string
.hang:
    cli
    hlt
    jmp  .hang

.load_success:
    mov  si, msg_stage2_ok
    call print_string

    ; Hand off execution to Stage 2 at 0x0000:0x8000, passing boot drive in DL
    mov  dl, [boot_drive]
    jmp  STAGE2_LOAD_SEG:STAGE2_LOAD_OFF

; ── Real-mode string print routine (BIOS teletype int 0x10) ──────────────────
print_string:
    push ax
    push bx
    mov  ah, 0x0E
    xor  bh, bh
.char_loop:
    lodsb
    test al, al
    jz   .done
    int  0x10
    jmp  .char_loop
.done:
    pop  bx
    pop  ax
    ret

; ── Data ─────────────────────────────────────────────────────────────────────
msg_stage1:     db 0x0D, 0x0A, "Nyota OS Bootloader (Stage 1)...", 0x0D, 0x0A, 0
msg_stage2_ok:  db "Stage 2 loaded. Transitioning...", 0x0D, 0x0A, 0
msg_disk_err:   db "FATAL: Failed to read Stage 2 from disk! System halted.", 0x0D, 0x0A, 0
boot_drive:     db 0

; ── Boot signature ───────────────────────────────────────────────────────────
times 510 - ($ - $$) db 0
dw 0xAA55
