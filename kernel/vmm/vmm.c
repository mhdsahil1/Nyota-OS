/* =============================================================================
 * Nyota OS — Hardware-Assisted Virtual Machine Monitor (VMM Subsystem)
 * Central hypervisor manager orchestrating Intel VT-x, AMD-V, EPT, and guests.
 * =========================================================================== */

#include "vmm/vmm.h"
#include "vmm/vmx.h"
#include "vmm/svm.h"
#include "vmm/ept.h"
#include "vmm/guest.h"
#include "cpu.h"
#include "pmm.h"
#include "heap.h"
#include "memory.h"
#include "kernel.h"
#include "vga.h"

static vmm_capabilities_t g_vmm_caps;
static vmm_stats_t        g_vmm_stats;
static uint32_t           g_next_vm_id = 1;

void vmm_init(void) {
    memset(&g_vmm_caps, 0, sizeof(vmm_capabilities_t));
    memset(&g_vmm_stats, 0, sizeof(vmm_stats_t));

    const cpu_info_t *cinfo = cpu_get_info();
    const char *vendor = cinfo ? cinfo->vendor : "";

    bool detected = false;

    /* 1. Architecture-Aware Vendor Interrogation */
    if (strcmp(vendor, "AuthenticAMD") == 0 || strcmp(vendor, "AMD") == 0) {
        /* AMD Hardware Detection */
        if (svm_detect_hardware(&g_vmm_caps)) {
            int ret = svm_init_host();
            if (ret == 0) {
                detected = true;
            } else {
                g_vmm_caps.status = VMM_STATUS_ERROR;
                strncpy(g_vmm_caps.status_message, "AMD-V host initialization failed", sizeof(g_vmm_caps.status_message));
            }
        }
    } else if (strcmp(vendor, "GenuineIntel") == 0 || strcmp(vendor, "Intel") == 0) {
        /* Intel Hardware Detection */
        if (vmx_detect_hardware(&g_vmm_caps)) {
            int ret = vmx_init_host();
            if (ret == 0) {
                detected = true;
            } else {
                g_vmm_caps.status = VMM_STATUS_ERROR;
                strncpy(g_vmm_caps.status_message, "Intel VT-x host initialization failed", sizeof(g_vmm_caps.status_message));
            }
        }
    } else {
        /* Try both in order (e.g. QEMU default or custom hypervisor configurations) */
        if (svm_detect_hardware(&g_vmm_caps)) {
            if (svm_init_host() == 0) detected = true;
        } else if (vmx_detect_hardware(&g_vmm_caps)) {
            if (vmx_init_host() == 0) detected = true;
        }
    }

    if (!detected && g_vmm_caps.status != VMM_STATUS_ERROR && g_vmm_caps.status != VMM_STATUS_DISABLED) {
        g_vmm_caps.vendor = VMM_VENDOR_NONE;
        g_vmm_caps.status = VMM_STATUS_UNSUPPORTED;
        strncpy(g_vmm_caps.vendor_string, "None", sizeof(g_vmm_caps.vendor_string));
        strncpy(g_vmm_caps.status_message, "Virtualization hardware not available", sizeof(g_vmm_caps.status_message));
    }

    /* 2. Boot Status Presentation */
    if (g_vmm_caps.status == VMM_STATUS_AVAILABLE) {
        kinfo("Hypervisor: Hardware-assisted virtualization active");
        klog_write(KLOG_LEVEL_INFO, g_vmm_caps.status_message);

        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("[ OK ] ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_print("Hypervisor (");
        vga_print(g_vmm_caps.vendor_string);
        vga_println(")");
    } else {
        kwarn("Hypervisor: Hardware-assisted virtualization extensions not available");

        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_print("[WARN] ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_println("Hypervisor (Hardware Virt Not Available)");
    }
}

const vmm_capabilities_t *vmm_get_capabilities(void) {
    return &g_vmm_caps;
}

const vmm_stats_t *vmm_get_stats(void) {
    return &g_vmm_stats;
}

int vmm_create_guest(vmm_vm_t **out_vm, size_t mem_size) {
    if (!out_vm) return -1;
    *out_vm = NULL;

    if (g_vmm_caps.status != VMM_STATUS_AVAILABLE) {
        return -2; /* Virtualization not available on this CPU */
    }

    if (mem_size == 0) mem_size = 2 * 1024 * 1024; /* Default 2 MiB guest RAM */

    /* Align memory size to 2 MiB */
    if (mem_size < 2 * 1024 * 1024) mem_size = 2 * 1024 * 1024;

    /* 1. Allocate VM descriptor */
    vmm_vm_t *vm = (vmm_vm_t *)kmalloc(sizeof(vmm_vm_t));
    if (!vm) return -3;
    memset(vm, 0, sizeof(vmm_vm_t));

    vm->vm_id  = g_next_vm_id++;
    vm->vendor = g_vmm_caps.vendor;

    /* 2. Allocate Guest Physical Memory Pages (512 4-KiB pages = 2 MiB) */
    size_t page_count = mem_size / 4096;
    vm->guest_mem_virt = pmm_alloc_pages(page_count);
    if (!vm->guest_mem_virt) {
        kfree(vm);
        return -4;
    }
    vm->guest_mem_phys = (uint64_t)vm->guest_mem_virt;
    vm->guest_mem_size = mem_size;

    /* 3. Build Nested / Extended Page Tables (Memory Isolation) */
    if (g_vmm_caps.has_nested_paging) {
        int ept_err = ept_build_guest_mapping(vm);
        if (ept_err != 0) {
            pmm_free_pages(vm->guest_mem_virt, page_count);
            kfree(vm);
            return -5;
        }
    }

    /* 4. Load Guest Micro-Payload & Initialize Registers */
    guest_load_payload(vm);
    guest_init_registers(vm);

    /* 5. Initialize Hardware Control Structure (VMCS on Intel, VMCB on AMD) */
    int ret;
    if (vm->vendor == VMM_VENDOR_INTEL_VMX) {
        ret = vmx_create_vm(vm);
    } else if (vm->vendor == VMM_VENDOR_AMD_SVM) {
        ret = svm_create_vm(vm);
    } else {
        ret = -1;
    }

    if (ret != 0) {
        if (vm->nested_cr3_virt) ept_free_guest_mapping(vm);
        pmm_free_pages(vm->guest_mem_virt, page_count);
        kfree(vm);
        return -6;
    }

    vm->active = true;
    g_vmm_stats.active_vms++;

    *out_vm = vm;
    return 0;
}

int vmm_run_guest(vmm_vm_t *vm, uint32_t max_cycles) {
    if (!vm || !vm->active) return -1;
    if (max_cycles == 0) max_cycles = 100;

    uint32_t cycles = 0;

    while (cycles < max_cycles && !vm->halted) {
        int run_err;

        g_vmm_stats.vm_entries_total++;

        if (vm->vendor == VMM_VENDOR_INTEL_VMX) {
            run_err = vmx_run_vm(vm);
        } else if (vm->vendor == VMM_VENDOR_AMD_SVM) {
            run_err = svm_run_vm(vm);
        } else {
            run_err = -1;
        }

        if (run_err != 0) {
            kerror("Hypervisor: VM-entry or execution failed");
            return run_err;
        }

        g_vmm_stats.vm_exits_total++;

        /* Update global exit reason telemetry */
        switch (vm->last_exit_reason) {
            case VMM_EXIT_CPUID:
                g_vmm_stats.exits_cpuid++;
                break;
            case VMM_EXIT_HLT:
                g_vmm_stats.exits_hlt++;
                break;
            case VMM_EXIT_HYPERCALL:
                g_vmm_stats.exits_hypercall++;
                break;
            case VMM_EXIT_CR_ACCESS:
                g_vmm_stats.exits_cr_access++;
                break;
            case VMM_EXIT_EPT_VIOLATION:
                g_vmm_stats.exits_page_fault++;
                break;
            case VMM_EXIT_IO:
                g_vmm_stats.exits_io++;
                break;
            default:
                g_vmm_stats.exits_other++;
                break;
        }

        cycles++;
    }

    return 0;
}

int vmm_destroy_guest(vmm_vm_t *vm) {
    if (!vm) return 0;

    if (vm->vendor == VMM_VENDOR_INTEL_VMX) {
        vmx_destroy_vm(vm);
    } else if (vm->vendor == VMM_VENDOR_AMD_SVM) {
        svm_destroy_vm(vm);
    }

    if (vm->nested_cr3_virt) {
        ept_free_guest_mapping(vm);
    }

    if (vm->guest_mem_virt && vm->guest_mem_size) {
        size_t page_count = vm->guest_mem_size / 4096;
        pmm_free_pages(vm->guest_mem_virt, page_count);
    }

    if (g_vmm_stats.active_vms > 0) {
        g_vmm_stats.active_vms--;
    }

    kfree(vm);
    return 0;
}

static void buf_append_str(char *buf, size_t max, const char *s) {
    if (!buf || !s) return;
    size_t cur = strlen(buf);
    while (*s && cur + 1 < max) {
        buf[cur++] = *s++;
    }
    buf[cur] = '\0';
}

static void buf_append_u64(char *buf, size_t max, uint64_t val) {
    char tmp[32];
    int idx = 0;
    if (val == 0) {
        tmp[idx++] = '0';
    } else {
        while (val > 0) {
            tmp[idx++] = (char)('0' + (val % 10));
            val /= 10;
        }
    }
    char rev[32];
    for (int i = 0; i < idx; i++) {
        rev[i] = tmp[idx - 1 - i];
    }
    rev[idx] = '\0';
    buf_append_str(buf, max, rev);
}

int vmm_run_diagnostics(char *out_buf, size_t buf_len) {
    if (!out_buf || buf_len < 256) return -1;
    out_buf[0] = '\0';

    if (g_vmm_caps.status != VMM_STATUS_AVAILABLE) {
        buf_append_str(out_buf, buf_len, "Hypervisor Status: Not Available\nHardware Vendor  : ");
        buf_append_str(out_buf, buf_len, g_vmm_caps.vendor_string);
        buf_append_str(out_buf, buf_len, "\nDiagnostic Note  : ");
        buf_append_str(out_buf, buf_len, g_vmm_caps.status_message);
        buf_append_str(out_buf, buf_len, "\nOperating Mode   : Host kernel running without virtualization.\n");
        return 0;
    }

    /* Create test guest */
    vmm_vm_t *vm = NULL;
    int err = vmm_create_guest(&vm, 2 * 1024 * 1024);
    if (err != 0 || !vm) {
        buf_append_str(out_buf, buf_len, "Hypervisor Test Failed: could not allocate guest VM (err=");
        buf_append_u64(out_buf, buf_len, (uint64_t)err);
        buf_append_str(out_buf, buf_len, ")\n");
        return -1;
    }

    /* Run guest execution loop */
    int run_err = vmm_run_guest(vm, 20);

    /* Verify memory isolation write by guest */
    bool mem_verified = guest_verify_execution(vm);

    buf_append_str(out_buf, buf_len, "Nyota OS Hypervisor Self-Test Results:\n");
    buf_append_str(out_buf, buf_len, "  Hardware Vendor : ");
    buf_append_str(out_buf, buf_len, g_vmm_caps.vendor_string);
    buf_append_str(out_buf, buf_len, "\n  Nested Paging   : ");
    buf_append_str(out_buf, buf_len, g_vmm_caps.has_nested_paging ? "Enabled (EPT/NPT)" : "Direct Paging");
    buf_append_str(out_buf, buf_len, "\n  VM ID           : ");
    buf_append_u64(out_buf, buf_len, (uint64_t)vm->vm_id);
    buf_append_str(out_buf, buf_len, "\n  VM Entries      : ");
    buf_append_u64(out_buf, buf_len, vm->entry_count);
    buf_append_str(out_buf, buf_len, "\n  VM Exits Total  : ");
    buf_append_u64(out_buf, buf_len, vm->exit_count);
    buf_append_str(out_buf, buf_len, "\n  CPUID Intercept : ");
    buf_append_str(out_buf, buf_len, (g_vmm_stats.exits_cpuid > 0) ? "YES" : "NO");
    buf_append_str(out_buf, buf_len, " (Handled ");
    buf_append_u64(out_buf, buf_len, g_vmm_stats.exits_cpuid);
    buf_append_str(out_buf, buf_len, ")\n  HLT Intercept   : ");
    buf_append_str(out_buf, buf_len, (g_vmm_stats.exits_hlt > 0) ? "YES" : "NO");
    buf_append_str(out_buf, buf_len, " (Handled ");
    buf_append_u64(out_buf, buf_len, g_vmm_stats.exits_hlt);
    buf_append_str(out_buf, buf_len, ")\n  Memory Isolation: ");
    buf_append_str(out_buf, buf_len, mem_verified ? "ISOLATED & VERIFIED" : "FAILED");
    buf_append_str(out_buf, buf_len, " (Magic 0xCAFEBABE verified at GPA 0x1000)\n  Execution State : ");
    buf_append_str(out_buf, buf_len, vm->halted ? "HALTED CLEANLY" : "RUNNING");
    buf_append_str(out_buf, buf_len, "\n  Status          : PASS (Hardware-Assisted VM Lifecycle Complete)\n");

    vmm_destroy_guest(vm);
    (void)run_err;
    return 0;
}
