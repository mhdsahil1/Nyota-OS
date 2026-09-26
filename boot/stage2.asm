; =============================================================================
; Nyota OS — Stage 2 Bootloader
; Target: x86 Real Mode (16-bit) -> x86_64 Long Mode (64-bit)
; Loaded at: 0x0000:0x8000
; Responsibilities:
;   1. Load Kernel binary from disk into temporary buffer (0x10000)
;   2. Enable A20 address line
;   3. Check CPUID and 64-bit Long Mode support
;   4. Set up 4-level PML4 paging structures (Identity map 0 - 16MB)
;   5. Set up 64-bit Global Descriptor Table (GDT)
;   6. Enable PAE, Long Mode, and Paging
;   7. Transition into 64-bit Long Mode
;   8. Relocate Kernel to 1MB mark (0x100000)
;   9. Transfer control to the 64-bit Kernel entry point
; =============================================================================

[org 0x8000]
[bits 16]

KERNEL_TEMP_BUF   equ 0x10000   ; Temporary buffer in low memory
KERNEL_TARGET_ADDR equ 0x100000  ; Final destination: 1 MB mark
KERNEL_SECTOR_CNT equ 125       ; Read 125 sectors (62.5 KB - fits within 64 KB segment)
KERNEL_START_LBA  equ 5         ; LBA 5 (Sector 0=boot, Sectors 1..4=stage2)

stage2_entry:
    cli
    cld

    mov  [stage2_drive], dl     ; Save boot drive

    ; Reset segments
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7C00
    sti

    mov  si, msg_stage2
    call puts16

    ; 1. Load kernel from disk
    call load_kernel_data

    ; 2. Detect BIOS physical memory map (E820)
    call detect_memory_e820

    ; 3. Enable A20 line
    call enable_a20

    ; 4. Verify CPU capabilities (CPUID & Long Mode)
    call check_cpu_long_mode

    ; 5. Prepare Page Tables (PML4, PDPT, PD)
    call setup_paging_tables

    ; 6. Disable interrupts for mode transition
    cli

    ; 7. Load 64-bit GDT
    lgdt [gdt64_desc]

    ; 7. Set CR3 to point to PML4 base address (0x1000)
    mov  eax, 0x1000
    mov  cr3, eax

    ; 8. Enable Physical Address Extension (PAE, bit 5 of CR4)
    mov  eax, cr4
    or   eax, (1 << 5)
    mov  cr4, eax

    ; 9. Enable Long Mode (LME, bit 8 of EFER MSR 0xC0000080)
    mov  ecx, 0xC0000080
    rdmsr
    or   eax, (1 << 8)
    wrmsr

    ; 10. Enable Paging (PG, bit 31) and Protected Mode (PE, bit 0) in CR0
    mov  eax, cr0
    or   eax, (1 << 31) | (1 << 0)
    mov  cr0, eax

    ; 11. Far jump into 64-bit code segment
    jmp  0x08:stage2_long_mode

; =============================================================================
; 16-bit Helper Routines
; =============================================================================

puts16:
    push ax
    push bx
    mov  ah, 0x0E
    xor  bh, bh
.loop:
    lodsb
    test al, al
    jz   .done
    int  0x10
    jmp  .loop
.done:
    pop  bx
    pop  ax
    ret

; ── Load kernel from disk into 0x10000 ────────────────────────────────────────
load_kernel_data:
    ; Try BIOS Extended Read (LBA packet int 0x13, ah=0x42)
    mov  si, dap_packet
    mov  dl, [stage2_drive]
    mov  ah, 0x42
    int  0x13
    jnc  .read_done

    ; Fallback to standard CHS read if extended read unsupported
    mov  di, 3
.chs_retry:
    mov  ax, 0x1000
    mov  es, ax
    xor  bx, bx                 ; ES:BX = 0x1000:0x0000 (physical 0x10000)
    mov  ah, 0x02
    mov  al, KERNEL_SECTOR_CNT
    mov  ch, 0                  ; Cylinder 0
    mov  cl, 6                  ; Sector 6 (1-based: LBA 5 + 1)
    mov  dh, 0                  ; Head 0
    mov  dl, [stage2_drive]
    int  0x13
    jnc  .read_done

    xor  ax, ax
    mov  dl, [stage2_drive]
    int  0x13
    dec  di
    jnz  .chs_retry

    ; Disk error
    mov  si, err_kernel_read
    call puts16
.hang:
    cli
    hlt
    jmp  .hang

.read_done:
    mov  si, msg_kernel_loaded
    call puts16
    ret

; ── Enable A20 Line ──────────────────────────────────────────────────────────
enable_a20:
    ; Check if already enabled
    call test_a20
    jnz  .a20_ok

    ; Try BIOS A20 gate function
    mov  ax, 0x2401
    int  0x15
    call test_a20
    jnz  .a20_ok

    ; Try Fast A20 gate via System Control Port A (0x92)
    in   al, 0x92
    test al, 2
    jnz  .a20_ok
    or   al, 2
    and  al, 0xFE               ; Prevent fast reset
    out  0x92, al

    call test_a20
    jnz  .a20_ok

    ; If still disabled, fatal error
    mov  si, err_a20
    call puts16
.hang:
    cli
    hlt
    jmp  .hang

.a20_ok:
    mov  si, msg_a20_ok
    call puts16
    ret

test_a20:
    push ax
    push cx
    push ds
    push es
    xor  ax, ax
    mov  ds, ax
    not  ax
    mov  es, ax
    mov  cx, [0x7DFE]           ; Memory at 0x0000:0x7DFE
    mov  ax, [es:0x7E0E]        ; Memory at 0xFFFF:0x7E0E (1MB wrap)
    cmp  cx, ax
    pop  es
    pop  ds
    pop  cx
    pop  ax
    ret

; ── CPUID & Long Mode Support Check ──────────────────────────────────────────
check_cpu_long_mode:
    ; 1. Check if CPUID is supported by toggling EFLAGS bit 21
    pushfd
    pop  eax
    mov  ecx, eax
    xor  eax, (1 << 21)
    push eax
    popfd
    pushfd
    pop  eax
    push ecx
    popfd
    cmp  eax, ecx
    jnz  .cpuid_supported

    mov  si, err_no_cpuid
    call puts16
    jmp  $

.cpuid_supported:
    ; 2. Check for extended CPUID functions
    mov  eax, 0x80000000
    cpuid
    cmp  eax, 0x80000001
    jb   .no_long_mode

    ; 3. Check for Long Mode (bit 29 of EDX in leaf 0x80000001)
    mov  eax, 0x80000001
    cpuid
    test edx, (1 << 29)
    jnz  .lm_supported

.no_long_mode:
    mov  si, err_no_lm
    call puts16
    jmp  $

.lm_supported:
    mov  si, msg_lm_ok
    call puts16
    ret

; ── Detect BIOS Physical Memory Map (E820) ───────────────────────────────────
E820_MAP_COUNT  equ 0x5000
E820_MAP_BUF    equ 0x5008
E820_SMAP_MAGIC equ 0x534D4150

detect_memory_e820:
    push es
    push di
    push ds
    push si
    push ebx
    push edx
    push ecx
    push bp

    xor  ax, ax
    mov  es, ax
    mov  ds, ax
    mov  di, E820_MAP_BUF
    xor  ebx, ebx
    xor  bp, bp

.e820_loop:
    mov  eax, 0xE820
    mov  edx, E820_SMAP_MAGIC
    mov  ecx, 24
    mov  dword [es:di + 20], 1  ; Default ACPI 3.0 attribute
    int  0x15
    jc   .e820_done

    cmp  eax, E820_SMAP_MAGIC
    jne  .e820_failed

    test ecx, ecx
    jz   .skip_entry
    cmp  cl, 20
    jb   .skip_entry

    inc  bp
    add  di, 24

.skip_entry:
    test ebx, ebx
    jz   .e820_done
    cmp  bp, 64
    jae  .e820_done
    jmp  .e820_loop

.e820_done:
    mov  word [ds:E820_MAP_COUNT], bp
    mov  word [ds:E820_MAP_COUNT + 2], 0
    mov  si, msg_mmap_ok
    call puts16
    pop  bp
    pop  ecx
    pop  edx
    pop  ebx
    pop  si
    pop  ds
    pop  di
    pop  es
    ret

.e820_failed:
    mov  dword [ds:E820_MAP_COUNT], 0
    pop  bp
    pop  ecx
    pop  edx
    pop  ebx
    pop  si
    pop  ds
    pop  di
    pop  es
    ret

; ── Page Table Setup (4-level Paging for Identity Map 0-128MB) ────────────────
setup_paging_tables:
    ; Clear 12 KB memory from 0x1000 to 0x4000 (PML4, PDPT, PD)
    mov  edi, 0x1000
    xor  eax, eax
    mov  ecx, 3072              ; 12288 bytes / 4 = 3072 dwords
    rep  stosd

    ; PML4[0] at 0x1000 points to PDPT at 0x2000 (Flags: Present=1, Writable=1)
    mov  dword [0x1000], 0x2003

    ; PDPT[0] at 0x2000 points to PD at 0x3000 (Flags: Present=1, Writable=1)
    mov  dword [0x2000], 0x3003

    ; PD at 0x3000 identity maps 0 - 128 MB using 64 2MB huge pages (Flag: 0x83)
    mov  edi, 0x3000
    mov  eax, 0x00000083        ; Base 0, Present=1, Writable=1, Page Size=2MB (Bit 7=1)
    mov  ecx, 64                ; 64 * 2MB = 128 MB (entries 64..511 remain zero/not present)
.map_pd:
    mov  dword [edi], eax
    mov  dword [edi + 4], 0
    add  eax, 0x00200000        ; Next 2MB boundary
    add  edi, 8
    loop .map_pd
    ret

; =============================================================================
; 64-bit Long Mode Transition
; =============================================================================

[bits 64]
default rel
stage2_long_mode:
    ; Reload data segment selectors with 64-bit kernel data descriptor (0x10)
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax

    ; Set initial 64-bit stack
    mov  rsp, 0x90000

    ; Copy Kernel from temporary load buffer (0x10000) to final destination (0x100000)
    mov  rsi, KERNEL_TEMP_BUF
    mov  rdi, KERNEL_TARGET_ADDR
    mov  rcx, (KERNEL_SECTOR_CNT * 512) / 8  ; Copy in 64-bit quadwords
    rep  movsq

    ; Transfer control directly to 64-bit Kernel at 0x100000
    mov  rax, KERNEL_TARGET_ADDR
    jmp  rax

; =============================================================================
; Data & Structures
; =============================================================================

align 4
dap_packet:
    db 0x10                     ; Packet size (16 bytes)
    db 0                        ; Reserved
    dw KERNEL_SECTOR_CNT        ; Number of sectors
    dw 0x0000                   ; Target buffer offset
    dw 0x1000                   ; Target buffer segment (0x1000:0x0000 = 0x10000)
    dq KERNEL_START_LBA         ; Starting LBA sector

align 8
gdt64:
    dq 0x0000000000000000       ; 0x00: Null descriptor
.code: equ $ - gdt64
    dq 0x00209A0000000000       ; 0x08: 64-bit Kernel Code (L=1, D=0, P=1, DPL=0)
.data: equ $ - gdt64
    dq 0x0000920000000000       ; 0x10: 64-bit Kernel Data (P=1, DPL=0, W=1)
gdt64_desc:
    dw $ - gdt64 - 1            ; Limit (Size - 1)
    dd gdt64                    ; Base address

stage2_drive:       db 0
msg_stage2:         db "Stage 2: Initializing CPU and loading kernel...", 0x0D, 0x0A, 0
msg_kernel_loaded:  db "Stage 2: Kernel loaded into memory.", 0x0D, 0x0A, 0
msg_a20_ok:         db "Stage 2: A20 gate verified.", 0x0D, 0x0A, 0
msg_lm_ok:          db "Stage 2: CPU Long Mode verified. Entering 64-bit...", 0x0D, 0x0A, 0
msg_mmap_ok:        db "Stage 2: E820 memory map detected.", 0x0D, 0x0A, 0
err_kernel_read:    db "FATAL: Kernel disk read failed! System halted.", 0x0D, 0x0A, 0
err_a20:            db "FATAL: Failed to enable A20 line! System halted.", 0x0D, 0x0A, 0
err_no_cpuid:       db "FATAL: CPU does not support CPUID! System halted.", 0x0D, 0x0A, 0
err_no_lm:          db "FATAL: 64-bit Long Mode unsupported by CPU! System halted.", 0x0D, 0x0A, 0

; Pad Stage 2 to exactly 2048 bytes (4 sectors)
times 2048 - ($ - $$) db 0
