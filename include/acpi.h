/* =============================================================================
 * Nyota OS — ACPI & MADT Table Parser (Phase 11 SMP)
 * Discovers system topology, Local APICs, I/O APICs, and CPU core counts.
 * =========================================================================== */

#ifndef NYOTA_ACPI_H
#define NYOTA_ACPI_H

#include "types.h"

#define MAX_DETECTED_CPUS 32

/* ACPI Table Header */
typedef struct __attribute__((packed)) {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_header_t;

/* Root System Description Pointer (RSDP) */
typedef struct __attribute__((packed)) {
    char signature[8];          /* "RSD PTR " */
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;           /* 0 = ACPI 1.0, 2 = ACPI 2.0+ */
    uint32_t rsdt_address;      /* 32-bit physical address of RSDT */
    /* ACPI 2.0+ fields */
    uint32_t length;
    uint64_t xsdt_address;      /* 64-bit physical address of XSDT */
    uint8_t extended_checksum;
    uint8_t reserved[3];
} acpi_rsdp_t;

/* Multiple APIC Description Table (MADT) Header */
typedef struct __attribute__((packed)) {
    acpi_header_t header;       /* Signature "APIC" */
    uint32_t lapic_address;     /* Local APIC MMIO physical address */
    uint32_t flags;             /* PCAT_COMPAT (bit 0): dual-8259 PIC present */
} acpi_madt_t;

/* MADT Record Header */
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
} acpi_madt_record_t;

#define MADT_TYPE_LOCAL_APIC            0
#define MADT_TYPE_IO_APIC               1
#define MADT_TYPE_INTERRUPT_OVERRIDE    2
#define MADT_TYPE_NMI_SOURCE            3
#define MADT_TYPE_LOCAL_APIC_NMI        4
#define MADT_TYPE_LOCAL_APIC_OVERRIDE   5

/* Type 0: Processor Local APIC */
typedef struct __attribute__((packed)) {
    acpi_madt_record_t header;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;             /* Bit 0 = Enabled, Bit 1 = Online Capable */
} acpi_madt_lapic_t;

/* Type 1: I/O APIC */
typedef struct __attribute__((packed)) {
    acpi_madt_record_t header;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_address;
    uint32_t global_system_interrupt_base;
} acpi_madt_ioapic_t;

/* Type 2: Interrupt Source Override */
typedef struct __attribute__((packed)) {
    acpi_madt_record_t header;
    uint8_t bus;                /* Constant 0 = ISA */
    uint8_t source;             /* Bus-relative interrupt source (IRQ) */
    uint32_t global_system_interrupt;
    uint16_t flags;
} acpi_madt_iso_t;

/* Discovered CPU Descriptor */
typedef struct {
    uint32_t cpu_id;            /* Logical CPU ID: 0, 1, 2... */
    uint32_t apic_id;           /* Hardware Local APIC ID */
    uint8_t acpi_id;
    bool enabled;
    bool is_bsp;
} acpi_cpu_t;

/* Public ACPI / MADT APIs */
void acpi_init(void);
bool acpi_is_available(void);
uint32_t acpi_get_cpu_count(void);
const acpi_cpu_t *acpi_get_cpu(uint32_t index);
uint64_t acpi_get_lapic_address(void);
uint32_t acpi_get_ioapic_address(void);
uint32_t acpi_get_bsp_apic_id(void);

#endif /* NYOTA_ACPI_H */
