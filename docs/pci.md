# Nyota OS PCI Hardware Enumeration & Subsystem

**Target Release**: Nyota OS v1.2.0  
**Architecture**: x86_64  
**Status**: Production / Tested  

---

## 1. Overview

Nyota OS implements dynamic PCI device discovery and resource allocation in [include/drivers/pci.h](file:///c:/Users/HP/Projects/Nyota%20OS/include/drivers/pci.h) and [kernel/drivers/pci/pci.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/drivers/pci/pci.c). The subsystem probes PCI configuration space, decodes device classifications, calculates Base Address Register (BAR) memory sizes, configures bus mastering, and exposes hardware telemetry to user applications.

---

## 2. Configuration Space Access

Access to standard 256-byte PCI configuration space is achieved via x86 I/O ports:
* **Address Port**: `0xCF8` (`PCI_CONFIG_ADDRESS`)
* **Data Port**: `0xCFC` (`PCI_CONFIG_DATA`)

The 32-bit address is constructed as:
```c
uint32_t address = (1U << 31)                      /* Enable Bit */
                 | ((uint32_t)bus << 16)          /* Bus Number (0..255) */
                 | ((uint32_t)device << 11)       /* Device Number (0..31) */
                 | ((uint32_t)function << 8)      /* Function Number (0..7) */
                 | (offset & 0xFC);               /* Register Offset (4-byte aligned) */
```

### Supported APIs
* `pci_read32(bus, dev, fn, reg)`
* `pci_read16(bus, dev, fn, reg)`
* `pci_read8(bus, dev, fn, reg)`
* `pci_write32(bus, dev, fn, reg, val)`
* `pci_write16(bus, dev, fn, reg, val)`
* `pci_write8(bus, dev, fn, reg, val)`

---

## 3. Hardware Discovery & Enumeration

During kernel boot (`kernel.c`), `pci_init()` executes recursive scanning across all 256 buses, 32 devices per bus, and up to 8 functions per device.

### Header Type & Multi-Function Support
* Header Type register at offset `0x0E` is inspected.
* Bit 7 (`0x80`) indicates a multi-function device. If bit 7 is clear on function 0, subsequent functions (1..7) are skipped for improved boot latency.

### Class & Subclass Decoding
Devices are decoded according to PCI specification revisions:

| Class Code | Class Name | Common Subclasses |
|------------|------------|-------------------|
| `0x00` | Unclassified Device | Non-VGA, VGA-compatible |
| `0x01` | Mass Storage Controller | IDE (`0x01`), SATA/AHCI (`0x06`), NVMe (`0x08`) |
| `0x02` | Network Controller | Ethernet (`0x00`), Wireless (`0x80`) |
| `0x03` | Display Controller | VGA compatible (`0x00`), XGA (`0x01`), 3D (`0x02`) |
| `0x04` | Multimedia Controller | Audio device (`0x01`), Audio controller (`0x03`) |
| `0x06` | Bridge Device | Host bridge (`0x00`), ISA bridge (`0x01`), PCI bridge (`0x04`) |
| `0x0C` | Serial Bus Controller | USB (`0x03`), SMBus (`0x05`) |

---

## 4. BAR Sizing and Resource Allocation

Nyota OS probes and sizes all 6 Base Address Registers (`BAR0` through `BAR5`):

```c
uint32_t pci_get_bar_size(pci_device_t *dev, uint8_t bar_index) {
    uint8_t reg = PCI_REG_BAR0 + (bar_index * 4);
    uint32_t original_bar = pci_read32(dev->bus, dev->device, dev->function, reg);

    /* Write 0xFFFFFFFF to request hardware decode mask */
    pci_write32(dev->bus, dev->device, dev->function, reg, 0xFFFFFFFF);
    uint32_t mask = pci_read32(dev->bus, dev->device, dev->function, reg);

    /* Restore original BAR */
    pci_write32(dev->bus, dev->device, dev->function, reg, original_bar);

    if (mask == 0 || mask == 0xFFFFFFFF) return 0;

    if (original_bar & 0x01) {
        /* I/O Port BAR: bits 1:0 reserved */
        return (~(mask & 0xFFFFFFFC) + 1);
    } else {
        /* MMIO BAR: bits 3:0 reserved */
        return (~(mask & 0xFFFFFFF0) + 1);
    }
}
```

### Bus Mastering
Device drivers enable bus mastering and memory-space decoding via `pci_enable_bus_mastering(pci_device_t *dev)`:
```c
uint16_t cmd = pci_read16(dev->bus, dev->device, dev->function, PCI_REG_COMMAND);
cmd |= PCI_CMD_IO_SPACE | PCI_CMD_MEM_SPACE | PCI_CMD_BUS_MASTER;
pci_write16(dev->bus, dev->device, dev->function, PCI_REG_COMMAND, cmd);
```

---

## 5. Diagnostic Tools & Syscalls

### System Call `SYS_PCI_INFO` (71)
Returns the array of detected PCI devices, class codes, vendor/device IDs, IRQ line, and BAR properties to userland.

### User Utility: `/bin/lspci`
The user utility `/bin/lspci` formats detected hardware into standard diagnostic reports:

```text
Nyota PCI Devices
-----------------
00:00.0 Host bridge [0x8086:0x1237] Class 0x06.0x00 (prog-if 0x00) IRQ 0
00:01.0 ISA bridge [0x8086:0x7000] Class 0x06.0x01 (prog-if 0x00) IRQ 0
00:01.1 IDE controller [0x8086:0x7010] Class 0x01.0x01 (prog-if 0x80) IRQ 0
00:01.3 Bridge device [0x8086:0x7113] Class 0x06.0x80 (prog-if 0x00) IRQ 9
00:02.0 VGA compatible controller [0x1234:0x1111] Class 0x03.0x00 (prog-if 0x00) IRQ 0 BAR0: 0xFD000000 (size 16384 KiB)
00:03.0 Ethernet controller [0x8086:0x100E] Class 0x02.0x00 (prog-if 0x00) IRQ 11 BAR0: 0xFEBC0000 (size 128 KiB)
00:04.0 SATA controller (AHCI) [0x8086:0x2922] Class 0x01.0x06 (prog-if 0x01) IRQ 11
00:05.0 NVM Express (NVMe) [0x1B36:0x0010] Class 0x01.0x08 (prog-if 0x02) IRQ 10 BAR0: 0xFEBF0000 (size 16 KiB)
```

Adding the `-v` flag reveals comprehensive BAR sizes, types, and resource maps.
