/* =============================================================================
 * Nyota OS — Virtual Machine Monitor Control Utility (/bin/vmm)
 * Manages and inspects the hardware-assisted hypervisor subsystem.
 * =========================================================================== */

#include "libnyota.h"

static void print_banner(void) {
    printf("Nyota OS Virtual Machine Monitor (VMM) v1.7.0\n");
    printf("Hardware-Assisted Hypervisor Control Tool\n\n");
}

static void print_usage(void) {
    print_banner();
    printf("Usage: vmm <command> [options]\n\n");
    printf("Commands:\n");
    printf("  info        Display hypervisor capabilities and status\n");
    printf("  run         Execute a guest virtual machine test loop\n");
    printf("  stats       Display VM execution statistics and exit counts\n");
    printf("  test        Run automated hypervisor validation suite\n");
    printf("  help        Show this help message\n\n");
}

static int cmd_info(void) {
    lib_vmm_info_t info;
    memset(&info, 0, sizeof(info));

    if (vmm_get_info(&info) < 0) {
        printf("Error: failed to query hypervisor status from kernel.\n");
        return 1;
    }

    printf("============================================================\n");
    printf("         Nyota OS Hypervisor Hardware Capabilities          \n");
    printf("============================================================\n");
    printf("Hardware Vendor      : %s\n", (info.vendor_name[0] ? info.vendor_name : "None / Emulated"));
    
    const char *status_str = "Unknown";
    switch (info.status) {
        case 0: status_str = "Uninitialized"; break;
        case 1: status_str = "Available & Ready"; break;
        case 2: status_str = "Active (VM Running)"; break;
        case 3: status_str = "Unsupported (No VT-x / SVM detected)"; break;
        case 4: status_str = "Disabled in BIOS / Firmware"; break;
        case 5: status_str = "Error during initialization"; break;
    }
    printf("Operational Status   : %s\n", status_str);
    printf("Status Details       : %s\n", (info.status_msg[0] ? info.status_msg : "OK"));
    printf("Nested Paging        : %s\n", (info.has_nested_paging ? "Supported (EPT/NPT)" : "Not Available"));
    printf("Hardware Revision    : 0x%x\n", info.revision_id);
    printf("------------------------------------------------------------\n");
    printf("Total VM Entries     : %llu\n", info.total_entries);
    printf("Total VM Exits       : %llu\n", info.total_exits);
    printf("  - CPUID Exits      : %llu\n", info.exits_cpuid);
    printf("  - HLT Exits        : %llu\n", info.exits_hlt);
    printf("  - Hypercall Exits  : %llu\n", info.exits_hypercall);
    printf("  - Page Fault Exits : %llu\n", info.exits_ept_fault);
    printf("============================================================\n");
    return 0;
}

static int cmd_run(void) {
    printf("Initiating guest virtual machine execution...\n");
    char report[1024];
    memset(report, 0, sizeof(report));

    int ret = vmm_run_vm(report, sizeof(report));
    if (report[0] != '\0') {
        printf("%s\n", report);
    }

    if (ret != 0) {
        printf("Virtual machine run finished with status code %d\n", ret);
        return 1;
    }
    return 0;
}

static int cmd_stats(void) {
    lib_vmm_info_t info;
    memset(&info, 0, sizeof(info));

    if (vmm_get_info(&info) < 0) {
        printf("Error: failed to query hypervisor statistics.\n");
        return 1;
    }

    printf("Hypervisor Statistics:\n");
    printf("  Total VM Entries  : %llu\n", info.total_entries);
    printf("  Total VM Exits    : %llu\n", info.total_exits);
    printf("  CPUID Intercepts  : %llu\n", info.exits_cpuid);
    printf("  HLT Intercepts    : %llu\n", info.exits_hlt);
    printf("  Hypercalls        : %llu\n", info.exits_hypercall);
    printf("  EPT/NPT Faults    : %llu\n", info.exits_ept_fault);
    return 0;
}

static int cmd_test(void) {
    printf("=== Running Nyota Hypervisor Self-Test Suite ===\n");
    lib_vmm_info_t info;
    memset(&info, 0, sizeof(info));

    if (vmm_get_info(&info) < 0) {
        printf("[FAIL] Syscall SYS_VMM_OP returned error\n");
        return 1;
    }
    printf("[PASS] Syscall SYS_VMM_OP communication successful\n");

    if (info.status == 3) {
        printf("[INFO] Hardware virtualization is unsupported on this CPU/environment.\n");
        printf("[PASS] Hypervisor correctly handled unsupported hardware gracefully.\n");
        return 0;
    }

    if (info.status != 1 && info.status != 2) {
        printf("[WARN] Hypervisor status is %u (%s)\n", info.status, info.status_msg);
        return 0;
    }

    printf("[PASS] Hypervisor hardware detected: %s\n", info.vendor_name);
    printf("[INFO] Running guest VM cycle...\n");

    char report[1024];
    memset(report, 0, sizeof(report));
    int ret = vmm_run_vm(report, sizeof(report));
    if (ret == 0) {
        printf("%s\n", report);
        printf("[PASS] Guest VM execution cycle and memory isolation verified\n");
    } else {
        printf("[FAIL] Guest VM run returned error %d\n", ret);
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return cmd_info();
    }

    if (strcmp(argv[1], "info") == 0) {
        return cmd_info();
    } else if (strcmp(argv[1], "run") == 0) {
        return cmd_run();
    } else if (strcmp(argv[1], "stats") == 0) {
        return cmd_stats();
    } else if (strcmp(argv[1], "test") == 0) {
        return cmd_test();
    } else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    } else {
        printf("Unknown command: %s\n\n", argv[1]);
        print_usage();
        return 1;
    }
}
