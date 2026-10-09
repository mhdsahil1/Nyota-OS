/* =============================================================================
 * Nyota OS — lspci User Utility (Phase 12)
 * Displays actual detected PCI devices from kernel enumeration via sys_pci_info.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    bool verbose = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: lspci [-v|--verbose]\n");
            printf("List all detected PCI hardware devices.\n");
            return 0;
        }
    }

    pci_info_list_t info;
    if (pci_info(&info) != 0) {
        printf("lspci: error querying PCI device information\n");
        return 1;
    }

    printf("Nyota PCI Devices\n");
    printf("-----------------\n");

    if (info.count == 0) {
        printf("No PCI devices detected.\n");
        return 0;
    }

    for (uint32_t i = 0; i < info.count; i++) {
        pci_info_entry_t *dev = &info.devices[i];
        printf("%02x:%02x.%u %s", dev->bus, dev->slot, dev->func, dev->description);
        if (verbose) {
            printf(" [%04x:%04x] Class %02x.%02x (prog-if %02x) IRQ %u",
                   dev->vendor_id, dev->device_id,
                   dev->class_code, dev->subclass, dev->prog_if,
                   dev->irq_line);
            if (dev->bar0_size > 0) {
                printf(" BAR0: 0x%lx (size %u KiB)",
                       (unsigned long)dev->bar0, (unsigned int)(dev->bar0_size / 1024));
            }
        }
        printf("\n");
    }

    return 0;
}
