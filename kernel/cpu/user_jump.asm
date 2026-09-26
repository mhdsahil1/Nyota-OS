; =============================================================================
; Nyota OS — Ring 3 User Mode Transition Routine (iretq)
; Sets up stack frame with user CS (0x23), user SS (0x1B), RFLAGS, and jumps.
; =============================================================================

[bits 64]
default rel
global user_enter_ring3

section .text
user_enter_ring3:
    ; Parameters:
    ; RDI = entry_point (or RCX for Windows ABI)
    ; RSI = user_rsp    (or RDX for Windows ABI)
    test rdi, rdi
    jnz  .abi_ready
    mov  rdi, rcx
    mov  rsi, rdx
.abi_ready:

    cli                         ; Disable interrupts while preparing iretq stack

    ; 1. Push user SS (User Data Selector = 0x18 | 3 = 0x1B)
    push qword 0x1B

    ; 2. Push user RSP
    push rsi

    ; 3. Push RFLAGS (IF=1 for interrupts enabled in user space; Bit 1 = 1)
    push qword 0x202

    ; 4. Push user CS (User Code Selector = 0x20 | 3 = 0x23)
    push qword 0x23

    ; 5. Push user RIP (Entry Point)
    push rdi

    ; 6. Load user data segment selector (0x1B) into data segment registers
    mov  ax, 0x1B
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax

    ; 7. Execute iretq: Hardware pops RIP, CS, RFLAGS, RSP, SS and switches CPL to 3!
    iretq
