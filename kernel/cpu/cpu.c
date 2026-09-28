/* =============================================================================
 * Nyota OS — CPU Information & Detection
 * Implements CPUID feature interrogation and CPU vendor detection.
 * =========================================================================== */

#include "cpu.h"
#include "kernel.h"
#include "vga.h"

static cpu_info_t cpu_info;

static inline void cpuid(uint32_t leaf, uint32_t subleaf,
                         uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid"
                      : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(leaf), "c"(subleaf));
}

void cpu_init(void) {
    uint32_t eax, ebx, ecx, edx;

    /* 1. Leaf 0: CPU Vendor String */
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);

    /* EBX, EDX, ECX form 12-byte string */
    *(uint32_t *)&cpu_info.vendor[0] = ebx;
    *(uint32_t *)&cpu_info.vendor[4] = edx;
    *(uint32_t *)&cpu_info.vendor[8] = ecx;
    cpu_info.vendor[12] = '\0';

    cpu_info.has_cpuid = true;

    /* 2. Leaf 1: Standard Features */
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    cpu_info.has_fpu  = (edx & (1 << 0))  != 0;
    cpu_info.has_pae  = (edx & (1 << 6))  != 0;
    cpu_info.has_apic = (edx & (1 << 9))  != 0;
    cpu_info.has_sse  = (edx & (1 << 25)) != 0;
    cpu_info.has_sse2 = (edx & (1 << 26)) != 0;
    cpu_info.has_sse3 = (ecx & (1 << 0))  != 0;

    /* 3. Leaf 0x80000000: Extended Function Query */
    cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000001) {
        cpuid(0x80000001, 0, &eax, &ebx, &ecx, &edx);
        cpu_info.has_nx        = (edx & (1 << 20)) != 0;
        cpu_info.has_long_mode = (edx & (1 << 29)) != 0;
    } else {
        cpu_info.has_nx = false;
        cpu_info.has_long_mode = false;
    }

    /* 4. Enable SSE / SSE2 hardware execution in CR0 & CR4 */
    if (cpu_info.has_sse) {
        uint64_t cr0;
        __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
        cr0 &= ~(1ULL << 2); /* Clear EM (Emulation) */
        cr0 |= (1ULL << 1);  /* Set MP (Monitor Coprocessor) */
        __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));

        uint64_t cr4;
        __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= (1ULL << 9);  /* OSFXSR: FXSAVE/FXRSTOR and SSE instructions */
        cr4 |= (1ULL << 10); /* OSXMMEXCPT: unmasked SSE FP exceptions */
        __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4));
    }
}

const cpu_info_t *cpu_get_info(void) {
    return &cpu_info;
}

void cpu_print_info(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_println("CPU Information");
    vga_println("-------------------------");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    vga_print("Vendor   : ");
    vga_println(cpu_info.vendor);

    vga_print("Mode     : ");
    vga_println(NYOTA_ARCH);

    vga_println("Features :");
    if (cpu_info.has_sse)       vga_println("  SSE");
    if (cpu_info.has_sse2)      vga_println("  SSE2");
    if (cpu_info.has_sse3)      vga_println("  SSE3");
    if (cpu_info.has_apic)      vga_println("  APIC");
    if (cpu_info.has_pae)       vga_println("  PAE");
    if (cpu_info.has_long_mode) vga_println("  Long Mode (x86_64)");
    if (cpu_info.has_nx)        vga_println("  NX (No-Execute)");
}
