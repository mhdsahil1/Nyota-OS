; =============================================================================
; Nyota OS — IDT Assembly Stubs
;
; For each interrupt/exception we define a small stub that:
;   1. Optionally pushes a dummy error code (for exceptions that don't push one)
;   2. Pushes the interrupt number
;   3. Jumps to a common handler that saves ALL registers and calls C
;
; ISRs 0-31  : CPU exceptions
; IRQs 0-15  : Hardware interrupts (remapped to ISRs 32-47 by PIC)
; =============================================================================

[bits 32]
[extern isr_handler]    ; C handler: void isr_handler(struct registers *)
[extern irq_handler]    ; C handler: void irq_handler(struct registers *)

; ── Macros ───────────────────────────────────────────────────────────────────
; CPU exceptions that do NOT push an error code automatically
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push dword 0        ; Dummy error code
    push dword %1       ; Interrupt number
    jmp  isr_common_stub
%endmacro

; CPU exceptions that DO push an error code (already on stack)
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push dword %1       ; Interrupt number (error code already pushed by CPU)
    jmp  isr_common_stub
%endmacro

; Hardware IRQ stubs — mapped to interrupt vector %2
%macro IRQ 2
global irq%1
irq%1:
    push dword 0        ; Dummy error code
    push dword %2       ; Interrupt number (IRQ %1 → vector %2)
    jmp  irq_common_stub
%endmacro

; ── CPU Exception ISRs (0–31) ─────────────────────────────────────────────────
ISR_NOERRCODE  0    ; #DE  Division By Zero
ISR_NOERRCODE  1    ; #DB  Debug
ISR_NOERRCODE  2    ;      NMI
ISR_NOERRCODE  3    ; #BP  Breakpoint
ISR_NOERRCODE  4    ; #OF  Overflow
ISR_NOERRCODE  5    ; #BR  BOUND Range Exceeded
ISR_NOERRCODE  6    ; #UD  Invalid Opcode
ISR_NOERRCODE  7    ; #NM  Device Not Available
ISR_ERRCODE    8    ; #DF  Double Fault          (error code = 0)
ISR_NOERRCODE  9    ;      Coprocessor Segment Overrun (legacy)
ISR_ERRCODE   10    ; #TS  Invalid TSS
ISR_ERRCODE   11    ; #NP  Segment Not Present
ISR_ERRCODE   12    ; #SS  Stack-Segment Fault
ISR_ERRCODE   13    ; #GP  General Protection Fault
ISR_ERRCODE   14    ; #PF  Page Fault
ISR_NOERRCODE 15    ;      Reserved
ISR_NOERRCODE 16    ; #MF  x87 FPU Error
ISR_ERRCODE   17    ; #AC  Alignment Check
ISR_NOERRCODE 18    ; #MC  Machine Check
ISR_NOERRCODE 19    ; #XM  SIMD Floating-Point
ISR_NOERRCODE 20    ;      Virtualisation
ISR_NOERRCODE 21    ;      Control Protection
ISR_NOERRCODE 22    ;      Reserved
ISR_NOERRCODE 23    ;      Reserved
ISR_NOERRCODE 24    ;      Reserved
ISR_NOERRCODE 25    ;      Reserved
ISR_NOERRCODE 26    ;      Reserved
ISR_NOERRCODE 27    ;      Reserved
ISR_NOERRCODE 28    ;      Reserved
ISR_NOERRCODE 29    ;      Reserved
ISR_ERRCODE   30    ; #SX  Security Exception
ISR_NOERRCODE 31    ;      Reserved

; ── Hardware IRQs (0–15) remapped to vectors 32–47 ────────────────────────────
IRQ  0, 32      ; Timer
IRQ  1, 33      ; Keyboard
IRQ  2, 34      ; Cascade (slave PIC)
IRQ  3, 35      ; COM2
IRQ  4, 36      ; COM1
IRQ  5, 37      ; LPT2
IRQ  6, 38      ; Floppy
IRQ  7, 39      ; LPT1 / Spurious
IRQ  8, 40      ; RTC
IRQ  9, 41      ; Free
IRQ 10, 42      ; Free
IRQ 11, 43      ; Free
IRQ 12, 44      ; PS/2 Mouse
IRQ 13, 45      ; FPU / Coprocessor
IRQ 14, 46      ; Primary ATA
IRQ 15, 47      ; Secondary ATA

; ── Common ISR stub (CPU exceptions) ─────────────────────────────────────────
; Stack on entry (top → bottom / lower → higher address):
;   int_no | err_code | eip | cs | eflags
;   (SS and ESP only pushed on privilege change — we stay in ring 0)
isr_common_stub:
    pusha                   ; Save EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI
    mov  ax, ds
    push eax                ; Save DS
    mov  ax, 0x10           ; Load kernel data segment
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    push esp                ; Pointer to 'struct registers' → first arg
    call isr_handler
    add  esp, 4             ; Remove the pointer argument
    pop  eax                ; Restore DS
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    popa                    ; Restore general-purpose registers
    add  esp, 8             ; Remove int_no and err_code
    iret

; ── Common IRQ stub (hardware interrupts) ─────────────────────────────────────
irq_common_stub:
    pusha
    mov  ax, ds
    push eax
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    push esp
    call irq_handler
    add  esp, 4
    pop  eax
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    popa
    add  esp, 8
    iret

; ── IDT load helper (called from C) ───────────────────────────────────────────
global idt_load
; void idt_load(uint32_t idt_ptr_addr);
idt_load:
    mov  eax, [esp + 4]
    lidt [eax]
    ret
