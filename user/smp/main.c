/* =============================================================================
 * Nyota OS — SMP Diagnostic Utility (/bin/smp)
 * Displays CPU discovery, APIC IDs, online cores, ticks, and active processes.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    bool verbose = false;
    if (argc > 1 && strcmp(argv[1], "-v") == 0) {
        verbose = true;
    }

    smp_info_t info;
    if (smp_info(&info) != 0) {
        printf("smp: failed to retrieve SMP information\n");
        return 1;
    }

    printf("Nyota SMP Information\n");
    printf("---------------------\n");
    printf("CPUs detected : %d\n", info.cpu_count);
    printf("CPUs online   : %d\n\n", info.cpu_online);

    if (verbose) {
        printf("CPU  APIC  State    Ticks   Switches  Syscalls  IPIs   Process\n");
        printf("------------------------------------------------------------\n");
        for (uint32_t i = 0; i < info.cpu_count; i++) {
            const char *state_str = info.cpus[i].online ? "ONLINE" : "OFFLINE";
            const char *proc_name = info.cpus[i].current_name[0] ? info.cpus[i].current_name : "idle";
            printf("CPU %d: APIC %d [%s] ticks=%d sw=%d sys=%d ipi=%d proc=%s\n",
                   (int)info.cpus[i].cpu_id,
                   (int)info.cpus[i].apic_id,
                   state_str,
                   (int)info.cpus[i].ticks,
                   (int)info.cpus[i].context_switches,
                   (int)info.cpus[i].syscall_count,
                   (int)info.cpus[i].ipi_count,
                   proc_name);
        }
    } else {
        printf("CPU  APIC  State    Ticks   Process\n");
        printf("-----------------------------------\n");
        for (uint32_t i = 0; i < info.cpu_count; i++) {
            const char *state_str = info.cpus[i].online ? "ONLINE" : "OFFLINE";
            const char *proc_name = info.cpus[i].current_name[0] ? info.cpus[i].current_name : "idle";
            printf("CPU %d: APIC %d [%s] ticks=%d proc=%s\n",
                   (int)info.cpus[i].cpu_id,
                   (int)info.cpus[i].apic_id,
                   state_str,
                   (int)info.cpus[i].ticks,
                   proc_name);
        }
    }

    return 0;
}
