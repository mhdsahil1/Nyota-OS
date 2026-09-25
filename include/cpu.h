#ifndef NYOTA_CPU_H
#define NYOTA_CPU_H

#include "types.h"

typedef struct {
    char vendor[13];
    bool has_cpuid;
    bool has_fpu;
    bool has_pae;
    bool has_apic;
    bool has_sse;
    bool has_sse2;
    bool has_sse3;
    bool has_long_mode;
    bool has_nx;
} cpu_info_t;

void cpu_init(void);
const cpu_info_t *cpu_get_info(void);
void cpu_print_info(void);

#endif /* NYOTA_CPU_H */
