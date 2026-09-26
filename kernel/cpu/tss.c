/* =============================================================================
 * Nyota OS — x86_64 Task State Segment (TSS)
 * Manages Ring 3 -> Ring 0 kernel stack switching (RSP0) and LTR loading.
 * =========================================================================== */

#include "tss.h"
#include "gdt.h"
#include "kernel.h"
#include "memory.h"

static tss_t kernel_tss __attribute__((aligned(16)));

/* Dedicated 16 KiB default kernel stack for Ring 3 -> Ring 0 transitions */
static uint8_t default_kernel_stack[16384] __attribute__((aligned(16)));

void tss_load(uint16_t selector) {
    __asm__ volatile ("ltr %0" : : "r"(selector));
}

void tss_init(void) {
    memset(&kernel_tss, 0, sizeof(kernel_tss));

    /* Configure RSP0 to top of default kernel stack */
    kernel_tss.rsp0 = (uint64_t)&default_kernel_stack[sizeof(default_kernel_stack)];

    /* Set I/O Map Base to size of TSS (no I/O permissions bitmap needed) */
    kernel_tss.iomap_base = sizeof(kernel_tss);

    /* Install TSS descriptor into GDT at selector 0x28 */
    gdt_set_tss((uint64_t)&kernel_tss, sizeof(kernel_tss) - 1);

    /* Load Task Register (TR) */
    tss_load(GDT_TSS_SEG);
}

void tss_set_rsp0(uint64_t rsp0) {
    kernel_tss.rsp0 = rsp0;
}

uint64_t tss_get_rsp0(void) {
    return kernel_tss.rsp0;
}
