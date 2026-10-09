/* =============================================================================
 * Nyota OS — ACPI & MADT Table Parser Implementation (Phase 11 SMP)
 * Discovers CPU topology, Local APIC MMIO address, and Application Processors.
 * =========================================================================== */

#include "acpi.h"
#include "kernel.h"
#include "memory.h"
#include "serial.h"

static bool acpi_available = false;
static uint64_t lapic_phys_addr = 0xFEE00000ULL;
static uint32_t ioapic_phys_addr = 0;
static uint32_t bsp_apic_id = 0;

static acpi_cpu_t acpi_cpus[MAX_DETECTED_CPUS];
static uint32_t acpi_cpu_count = 0;

/* Read CPUID leaf to retrieve initial APIC ID */
static uint32_t read_bsp_apic_id_cpuid(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile ("cpuid"
                      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      : "a"(1), "c"(0));
    return (ebx >> 24) & 0xFF;
}

/* Validate checksum of generic ACPI table */
static bool acpi_validate_checksum(const void *ptr, size_t length) {
    const uint8_t *bytes = (const uint8_t *)ptr;
    uint8_t sum = 0;
    for (size_t i = 0; i < length; i++) {
        sum += bytes[i];
    }
    return (sum == 0);
}

/* Scan memory region for RSDP signature "RSD PTR " */
static acpi_rsdp_t *find_rsdp_in_range(uint64_t start, uint64_t end) {
    for (uint64_t addr = start; addr < end; addr += 16) {
        if (memcmp((const void *)addr, "RSD PTR ", 8) == 0) {
            acpi_rsdp_t *candidate = (acpi_rsdp_t *)addr;
            if (acpi_validate_checksum(candidate, 20)) {
                return candidate;
            }
        }
    }
    return NULL;
}

/* Locate ACPI Root System Description Pointer (RSDP) */
static acpi_rsdp_t *acpi_find_rsdp(void) {
    /* 1. Search first 1 KiB of Extended BIOS Data Area (EBDA) */
    uint16_t ebda_segment = *(const uint16_t *)0x040E;
    if (ebda_segment != 0) {
        uint64_t ebda_base = ((uint64_t)ebda_segment) << 4;
        if (ebda_base >= 0x80000 && ebda_base < 0xA0000) {
            acpi_rsdp_t *rsdp = find_rsdp_in_range(ebda_base, ebda_base + 1024);
            if (rsdp) return rsdp;
        }
    }

    /* 2. Search BIOS Read-Only Memory space (0x000E0000 - 0x000FFFFF) */
    return find_rsdp_in_range(0x000E0000, 0x00100000);
}

/* Parse Multiple APIC Description Table (MADT) */
static void parse_madt(const acpi_madt_t *madt) {
    if (!madt || madt->header.length < sizeof(acpi_madt_t)) return;

    lapic_phys_addr = (uint64_t)madt->lapic_address;

    const uint8_t *ptr = (const uint8_t *)madt + sizeof(acpi_madt_t);
    const uint8_t *end = (const uint8_t *)madt + madt->header.length;

    while (ptr + sizeof(acpi_madt_record_t) <= end) {
        const acpi_madt_record_t *rec = (const acpi_madt_record_t *)ptr;
        if (rec->length == 0 || ptr + rec->length > end) break;

        switch (rec->type) {
            case MADT_TYPE_LOCAL_APIC: {
                const acpi_madt_lapic_t *lap = (const acpi_madt_lapic_t *)rec;
                /* Flags: Bit 0 = Enabled, Bit 1 = Online Capable */
                if ((lap->flags & 1) || (lap->flags & 2)) {
                    if (acpi_cpu_count < MAX_DETECTED_CPUS) {
                        acpi_cpu_t *c = &acpi_cpus[acpi_cpu_count];
                        c->cpu_id = acpi_cpu_count;
                        c->apic_id = lap->apic_id;
                        c->acpi_id = lap->acpi_processor_id;
                        c->enabled = true;
                        c->is_bsp = (lap->apic_id == bsp_apic_id);
                        acpi_cpu_count++;
                    }
                }
                break;
            }
            case MADT_TYPE_IO_APIC: {
                const acpi_madt_ioapic_t *io = (const acpi_madt_ioapic_t *)rec;
                ioapic_phys_addr = io->ioapic_address;
                break;
            }
            case MADT_TYPE_LOCAL_APIC_OVERRIDE: {
                /* 64-bit Local APIC address override structure */
                typedef struct __attribute__((packed)) {
                    acpi_madt_record_t header;
                    uint16_t reserved;
                    uint64_t lapic_phys;
                } acpi_madt_lapic_override_t;

                const acpi_madt_lapic_override_t *ovr = (const acpi_madt_lapic_override_t *)rec;
                lapic_phys_addr = ovr->lapic_phys;
                break;
            }
            default:
                break;
        }

        ptr += rec->length;
    }
}

void acpi_init(void) {
    acpi_available = false;
    acpi_cpu_count = 0;
    memset(acpi_cpus, 0, sizeof(acpi_cpus));

    bsp_apic_id = read_bsp_apic_id_cpuid();

    acpi_rsdp_t *rsdp = acpi_find_rsdp();
    if (!rsdp) {
        kwarn("[ACPI] RSDP not found, using uniprocessor fallback");
        goto fallback;
    }

    const acpi_madt_t *madt = NULL;

    /* Check if 64-bit XSDT is present (ACPI 2.0+) */
    if (rsdp->revision >= 2 && rsdp->xsdt_address != 0) {
        const acpi_header_t *xsdt = (const acpi_header_t *)rsdp->xsdt_address;
        if (acpi_validate_checksum(xsdt, xsdt->length)) {
            size_t entries = (xsdt->length - sizeof(acpi_header_t)) / sizeof(uint64_t);
            const uint64_t *table_ptrs = (const uint64_t *)((const uint8_t *)xsdt + sizeof(acpi_header_t));
            for (size_t i = 0; i < entries; i++) {
                const acpi_header_t *hdr = (const acpi_header_t *)table_ptrs[i];
                if (hdr && memcmp(hdr->signature, "APIC", 4) == 0 && acpi_validate_checksum(hdr, hdr->length)) {
                    madt = (const acpi_madt_t *)hdr;
                    break;
                }
            }
        }
    }

    /* Fallback to 32-bit RSDT */
    if (!madt && rsdp->rsdt_address != 0) {
        const acpi_header_t *rsdt = (const acpi_header_t *)(uint64_t)rsdp->rsdt_address;
        if (acpi_validate_checksum(rsdt, rsdt->length)) {
            size_t entries = (rsdt->length - sizeof(acpi_header_t)) / sizeof(uint32_t);
            const uint32_t *table_ptrs = (const uint32_t *)((const uint8_t *)rsdt + sizeof(acpi_header_t));
            for (size_t i = 0; i < entries; i++) {
                const acpi_header_t *hdr = (const acpi_header_t *)(uint64_t)table_ptrs[i];
                if (hdr && memcmp(hdr->signature, "APIC", 4) == 0 && acpi_validate_checksum(hdr, hdr->length)) {
                    madt = (const acpi_madt_t *)hdr;
                    break;
                }
            }
        }
    }

    if (madt) {
        parse_madt(madt);
        if (acpi_cpu_count > 0) {
            acpi_available = true;

            /* Ensure BSP is at index 0 */
            int bsp_idx = -1;
            for (uint32_t i = 0; i < acpi_cpu_count; i++) {
                if (acpi_cpus[i].apic_id == bsp_apic_id) {
                    bsp_idx = (int)i;
                    break;
                }
            }
            if (bsp_idx > 0) {
                acpi_cpu_t tmp = acpi_cpus[0];
                acpi_cpus[0] = acpi_cpus[bsp_idx];
                acpi_cpus[bsp_idx] = tmp;
                acpi_cpus[0].cpu_id = 0;
                acpi_cpus[bsp_idx].cpu_id = (uint32_t)bsp_idx;
            }
            acpi_cpus[0].is_bsp = true;
            return;
        }
    }

fallback:
    /* Uniprocessor / default fallback */
    acpi_cpu_count = 1;
    acpi_cpus[0].cpu_id = 0;
    acpi_cpus[0].apic_id = bsp_apic_id;
    acpi_cpus[0].acpi_id = 0;
    acpi_cpus[0].enabled = true;
    acpi_cpus[0].is_bsp = true;
    acpi_available = false;
}

bool acpi_is_available(void) {
    return acpi_available;
}

uint32_t acpi_get_cpu_count(void) {
    return acpi_cpu_count;
}

const acpi_cpu_t *acpi_get_cpu(uint32_t index) {
    if (index >= acpi_cpu_count) return NULL;
    return &acpi_cpus[index];
}

uint64_t acpi_get_lapic_address(void) {
    return lapic_phys_addr;
}

uint32_t acpi_get_ioapic_address(void) {
    return ioapic_phys_addr;
}

uint32_t acpi_get_bsp_apic_id(void) {
    return bsp_apic_id;
}
