/* =============================================================================
 * Nyota OS — Peripheral Component Interconnect (PCI) Implementation
 * Enumerates PCI bus, retrieves device BARs and IRQ lines, enables bus mastering.
 * =========================================================================== */

#include "drivers/pci.h"
#include "io.h"
#include "memory.h"
#include "kernel.h"
#include "serial.h"
#include "vga.h"

static pci_device_t pci_device_table[PCI_MAX_DEVICES];
static size_t pci_device_count = 0;

/* ── Low-Level PCI Config Space Access ─────────────────────────────────────── */

static inline uint32_t pci_config_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint32_t)(
        (1U << 31) |
        ((uint32_t)bus << 16) |
        ((uint32_t)(slot & 0x1F) << 11) |
        ((uint32_t)(func & 0x07) << 8) |
        (offset & 0xFC)
    );
}

uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_config_addr(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    outl(PCI_CONFIG_ADDRESS, pci_config_addr(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, val);
}

uint16_t pci_read_config_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t dword = pci_read_config_dword(bus, slot, func, offset);
    return (uint16_t)((dword >> ((offset & 2) * 8)) & 0xFFFF);
}

void pci_write_config_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val) {
    uint32_t dword = pci_read_config_dword(bus, slot, func, offset);
    uint32_t shift = (offset & 2) * 8;
    dword &= ~(0xFFFFU << shift);
    dword |= ((uint32_t)val << shift);
    pci_write_config_dword(bus, slot, func, offset, dword);
}

uint8_t pci_read_config_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t dword = pci_read_config_dword(bus, slot, func, offset);
    return (uint8_t)((dword >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write_config_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t val) {
    uint32_t dword = pci_read_config_dword(bus, slot, func, offset);
    uint32_t shift = (offset & 3) * 8;
    dword &= ~(0xFFU << shift);
    dword |= ((uint32_t)val << shift);
    pci_write_config_dword(bus, slot, func, offset, dword);
}

/* ── BAR Interrogation ─────────────────────────────────────────────────────── */

static void pci_read_bars(pci_device_t *dev) {
    for (int b = 0; b < 6; b++) {
        uint8_t bar_off = (uint8_t)(PCI_REG_BAR0 + b * 4);
        uint32_t orig = pci_read_config_dword(dev->bus, dev->slot, dev->func, bar_off);

        /* Write all 1s to discover BAR size */
        pci_write_config_dword(dev->bus, dev->slot, dev->func, bar_off, 0xFFFFFFFF);
        uint32_t mask = pci_read_config_dword(dev->bus, dev->slot, dev->func, bar_off);
        /* Restore original BAR value */
        pci_write_config_dword(dev->bus, dev->slot, dev->func, bar_off, orig);

        if (orig & 0x01) {
            /* I/O Space BAR */
            dev->bar_is_io[b] = true;
            dev->bar[b] = orig & ~0x03;
            dev->bar_size[b] = (mask != 0) ? (~(mask & ~0x03) + 1) : 0;
        } else {
            /* Memory Space BAR */
            dev->bar_is_io[b] = false;
            uint8_t type = (uint8_t)((orig >> 1) & 0x03);

            if (type == 0x02 && b < 5) {
                /* 64-bit Memory BAR */
                uint32_t orig_hi = pci_read_config_dword(dev->bus, dev->slot, dev->func, bar_off + 4);
                pci_write_config_dword(dev->bus, dev->slot, dev->func, bar_off + 4, 0xFFFFFFFF);
                uint32_t mask_hi = pci_read_config_dword(dev->bus, dev->slot, dev->func, bar_off + 4);
                pci_write_config_dword(dev->bus, dev->slot, dev->func, bar_off + 4, orig_hi);

                dev->bar[b] = (((uint64_t)orig_hi << 32) | (orig & ~0x0FULL));
                uint64_t full_mask = (((uint64_t)mask_hi << 32) | (mask & ~0x0FULL));
                dev->bar_size[b] = (full_mask != 0) ? (~full_mask + 1) : 0;

                /* Skip next BAR slot as it was consumed as high 32 bits */
                b++;
                dev->bar[b] = 0;
                dev->bar_size[b] = 0;
                dev->bar_is_io[b] = false;
            } else {
                /* 32-bit Memory BAR */
                dev->bar[b] = orig & ~0x0F;
                dev->bar_size[b] = (mask != 0) ? (~(mask & ~0x0F) + 1) : 0;
            }
        }
    }
}

/* ── Bus Enumeration ──────────────────────────────────────────────────────── */

static void pci_check_function(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t vendor_id = pci_read_config_word(bus, slot, func, PCI_REG_VENDOR_ID);
    if (vendor_id == 0xFFFF || vendor_id == 0x0000) {
        return; /* No device present */
    }

    if (pci_device_count >= PCI_MAX_DEVICES) {
        return;
    }

    pci_device_t *dev = &pci_device_table[pci_device_count];
    memset(dev, 0, sizeof(pci_device_t));

    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = vendor_id;
    dev->device_id = pci_read_config_word(bus, slot, func, PCI_REG_DEVICE_ID);
    dev->class_code = pci_read_config_byte(bus, slot, func, PCI_REG_CLASS_CODE);
    dev->subclass = pci_read_config_byte(bus, slot, func, PCI_REG_SUBCLASS);
    dev->prog_if = pci_read_config_byte(bus, slot, func, PCI_REG_PROG_IF);
    dev->revision_id = pci_read_config_byte(bus, slot, func, PCI_REG_REVISION_ID);
    dev->irq_line = pci_read_config_byte(bus, slot, func, PCI_REG_INTERRUPT_LINE);
    dev->irq_pin = pci_read_config_byte(bus, slot, func, PCI_REG_INTERRUPT_PIN);

    pci_read_bars(dev);

    pci_device_count++;

    /* Diagnostic log */
    serial_write("[PCI] ");
    serial_write_hex(dev->bus); serial_write(":");
    serial_write_hex(dev->slot); serial_write(".");
    serial_write_dec(dev->func); serial_write(" Device [");
    serial_write_hex(dev->vendor_id); serial_write(":");
    serial_write_hex(dev->device_id); serial_write("] Class ");
    serial_write_hex(dev->class_code); serial_write(":");
    serial_write_hex(dev->subclass); serial_write(" IRQ ");
    serial_write_dec(dev->irq_line);
    if (dev->bar[0]) {
        serial_write(" BAR0: "); serial_write_hex(dev->bar[0]);
    }
    serial_write("\n");
}

static void pci_scan_bus(uint8_t bus) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        uint16_t vendor_id = pci_read_config_word(bus, slot, 0, PCI_REG_VENDOR_ID);
        if (vendor_id == 0xFFFF || vendor_id == 0x0000) {
            continue;
        }

        pci_check_function(bus, slot, 0);

        uint8_t header_type = pci_read_config_byte(bus, slot, 0, PCI_REG_HEADER_TYPE);
        if (header_type & 0x80) {
            /* Multi-function device: scan functions 1..7 */
            for (uint8_t func = 1; func < 8; func++) {
                pci_check_function(bus, slot, func);
            }
        }
    }
}

void pci_init(void) {
    pci_device_count = 0;
    memset(pci_device_table, 0, sizeof(pci_device_table));

    /* Scan PCI bus 0 */
    pci_scan_bus(0);

    /* If bus 0 has multi-function host bridge, scan other busses */
    uint8_t header_type = pci_read_config_byte(0, 0, 0, PCI_REG_HEADER_TYPE);
    if (header_type & 0x80) {
        for (uint8_t bus = 1; bus < 8; bus++) {
            uint16_t v = pci_read_config_word(bus, 0, 0, PCI_REG_VENDOR_ID);
            if (v != 0xFFFF && v != 0x0000) {
                pci_scan_bus(bus);
            }
        }
    }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("PCI (");
    vga_print_dec((uint64_t)pci_device_count);
    vga_println(" devices)");
}

/* ── Device Lookup Helpers ────────────────────────────────────────────────── */

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (size_t i = 0; i < pci_device_count; i++) {
        if (pci_device_table[i].vendor_id == vendor_id &&
            pci_device_table[i].device_id == device_id) {
            return &pci_device_table[i];
        }
    }
    return NULL;
}

pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass) {
    for (size_t i = 0; i < pci_device_count; i++) {
        if (pci_device_table[i].class_code == class_code &&
            pci_device_table[i].subclass == subclass) {
            return &pci_device_table[i];
        }
    }
    return NULL;
}

void pci_enable_bus_master(pci_device_t *dev) {
    if (!dev) return;
    uint16_t cmd = pci_read_config_word(dev->bus, dev->slot, dev->func, PCI_REG_COMMAND);
    cmd |= (PCI_CMD_BUS_MASTER | PCI_CMD_MEMORY_SPACE | PCI_CMD_IO_SPACE);
    pci_write_config_word(dev->bus, dev->slot, dev->func, PCI_REG_COMMAND, cmd);
}

size_t pci_get_device_count(void) {
    return pci_device_count;
}

pci_device_t *pci_get_device(size_t index) {
    if (index >= pci_device_count) return NULL;
    return &pci_device_table[index];
}
