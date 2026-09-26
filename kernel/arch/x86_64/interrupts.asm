; =============================================================================
; Nyota OS — Assembly Interrupt Stubs (x86_64)
; Generates 256 ISR stubs, builds uniform interrupt frame, calls C dispatcher.
; =============================================================================

[bits 64]
default rel

section .text
extern interrupt_dispatch

; Macro for exceptions/interrupts without CPU error code
%macro ISR_NOERR 1
global isr_stub_%1
isr_stub_%1:
    push qword 0          ; Push dummy error code
    push qword %1         ; Push interrupt vector number
    jmp  isr_common_stub
%endmacro

; Macro for CPU exceptions that push their own error code
%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    ; CPU already pushed error code on stack
    push qword %1         ; Push interrupt vector number
    jmp  isr_common_stub
%endmacro

; ── Exception Stubs (Vectors 0..31) ──────────────────────────────────────────
ISR_NOERR 0   ; Divide-by-zero Error (#DE)
ISR_NOERR 1   ; Debug Exception (#DB)
ISR_NOERR 2   ; Non-Maskable Interrupt (NMI)
ISR_NOERR 3   ; Breakpoint Exception (#BP)
ISR_NOERR 4   ; Overflow (#OF)
ISR_NOERR 5   ; Bound Range Exceeded (#BR)
ISR_NOERR 6   ; Invalid Opcode (#UD)
ISR_NOERR 7   ; Device Not Available (#NM)
ISR_ERR   8   ; Double Fault (#DF) - HAS ERROR CODE
ISR_NOERR 9   ; Coprocessor Segment Overrun
ISR_ERR   10  ; Invalid TSS (#TS) - HAS ERROR CODE
ISR_ERR   11  ; Segment Not Present (#NP) - HAS ERROR CODE
ISR_ERR   12  ; Stack-Segment Fault (#SS) - HAS ERROR CODE
ISR_ERR   13  ; General Protection Fault (#GP) - HAS ERROR CODE
ISR_ERR   14  ; Page Fault (#PF) - HAS ERROR CODE
ISR_NOERR 15  ; Reserved
ISR_NOERR 16  ; x87 Floating-Point Exception (#MF)
ISR_ERR   17  ; Alignment Check (#AC) - HAS ERROR CODE
ISR_NOERR 18  ; Machine Check (#MC)
ISR_NOERR 19  ; SIMD Floating-Point Exception (#XM/#XF)
ISR_NOERR 20  ; Virtualization Exception (#VE)
ISR_ERR   21  ; Control Protection Exception (#CP) - HAS ERROR CODE
ISR_NOERR 22  ; Reserved
ISR_NOERR 23  ; Reserved
ISR_NOERR 24  ; Reserved
ISR_NOERR 25  ; Reserved
ISR_NOERR 26  ; Reserved
ISR_NOERR 27  ; Reserved
ISR_NOERR 28  ; Hypervisor Injection Exception (#HV)
ISR_ERR   29  ; VMM Communication Exception (#VC) - HAS ERROR CODE
ISR_ERR   30  ; Security Exception (#SX) - HAS ERROR CODE
ISR_NOERR 31  ; Reserved

; ── IRQ & Software Interrupt Stubs (Vectors 32..255) ─────────────────────────
%assign i 32
%rep 224
ISR_NOERR i
%assign i i+1
%endrep

; ── Common Interrupt Handler Stub ────────────────────────────────────────────
align 16
isr_common_stub:
    ; 1. Save all 15 general-purpose registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; 2. Clear Direction Flag as required by System V AMD64 ABI
    cld

    ; 3. Pass pointer to interrupt_frame_t as first parameter
    ; (RDI for System V ABI, RCX for Windows ABI compatibility)
    mov  rdi, rsp
    mov  rcx, rsp

    ; 4. Call C interrupt dispatcher
    call interrupt_dispatch

    ; 5. Restore general-purpose registers
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rbp
    pop  rdi
    pop  rsi
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax

    ; 6. Clean up vector number (8 bytes) and error code (8 bytes)
    add  rsp, 16

    ; 7. Return from interrupt
    iretq

; ── 256-entry Table of ISR Function Pointers ─────────────────────────────────
section .rodata
global isr_stub_table
align 8
isr_stub_table:
%assign i 0
%rep 256
    dq isr_stub_%+i
%assign i i+1
%endrep
