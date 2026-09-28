/* =============================================================================
 * Nyota OS — Peripheral Component Interconnect (PCI) Driver Interface
 * Bus enumeration, configuration space access, BAR discovery, and device lookups.
 * =========================================================================== */

#ifndef NYOTA_DRIVERS_PCI_H
#define NYOTA_DRIVERS_PCI_H

#include "types.h"

#define PCI_CONFIG_ADDRESS      0x0CF8
#define PCI_CONFIG_DATA         0x0CFC

#define PCI_MAX_DEVICES         32

/* PCI Configuration Registers */
#define PCI_REG_VENDOR_ID       0x00
#define PCI_REG_DEVICE_ID       0x02
#define PCI_REG_COMMAND         0x04
#define PCI_REG_STATUS          0x06
#define PCI_REG_REVISION_ID     0x08
#define PCI_REG_PROG_IF         0x09
#define PCI_REG_SUBCLASS        0x0A
#define PCI_REG_CLASS_CODE      0x0B
#define PCI_REG_CACHE_LINE_SIZE 0x0C
#define PCI_REG_LATENCY_TIMER   0x0D
#define PCI_REG_HEADER_TYPE     0x0E
#define PCI_REG_BIST            0x0F
#define PCI_REG_BAR0            0x10
#define PCI_REG_BAR1            0x14
#define PCI_REG_BAR2            0x18
#define PCI_REG_BAR3            0x1C
#define PCI_REG_BAR4            0x20
#define PCI_REG_BAR5            0x24
#define PCI_REG_INTERRUPT_LINE  0x3C
#define PCI_REG_INTERRUPT_PIN   0x3D

/* Command Register Bits */
#define PCI_CMD_IO_SPACE        (1 << 0)
#define PCI_CMD_MEMORY_SPACE    (1 << 1)
#define PCI_CMD_BUS_MASTER      (1 << 2)

/* Common PCI Classes */
#define PCI_CLASS_MASS_STORAGE  0x01
#define PCI_CLASS_NETWORK       0x02
#define PCI_CLASS_DISPLAY       0x03
#define PCI_CLASS_BRIDGE        0x06

/* PCI Subclasses */
#define PCI_SUBCLASS_ETHERNET   0x00

typedef struct pci_device {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision_id;

    uint8_t  irq_line;
    uint8_t  irq_pin;

    uint64_t bar[6];
    uint64_t bar_size[6];
    bool     bar_is_io[6];
} pci_device_t;

/* PCI APIs */
void pci_init(void);

uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_write_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
uint16_t pci_read_config_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_write_config_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);
uint8_t  pci_read_config_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_write_config_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t val);

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);
pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass);
void          pci_enable_bus_master(pci_device_t *dev);

size_t        pci_get_device_count(void);
pci_device_t *pci_get_device(size_t index);

#endif /* NYOTA_DRIVERS_PCI_H */
