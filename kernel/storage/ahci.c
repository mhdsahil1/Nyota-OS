/* =============================================================================
 * Nyota OS — Advanced Host Controller Interface (AHCI / SATA) Driver
 * Detects AHCI controllers via PCI, resets HBA, initializes SATA ports,
 * executes DMA sector I/O, and registers "sda", "sdb", etc. block devices.
 * =========================================================================== */

#include "storage/ahci.h"
#include "storage/block.h"
#include "drivers/pci.h"
#include "paging.h"
#include "pmm.h"
#include "memory.h"
#include "vga.h"
#include "serial.h"
#include "kernel.h"
#include "spinlock.h"

static ahci_controller_t ahci_ctrl;
static spinlock_t ahci_lock = SPINLOCK_INIT;
static uint8_t *ahci_dma_bounce = NULL;

/* ── Port Helper Functions ────────────────────────────────────────────────── */

static int ahci_stop_port(ahci_port_reg_t *port) {
    port->cmd &= ~(1U << 0);  /* Clear ST (bit 0) */
    port->cmd &= ~(1U << 4);  /* Clear FRE (bit 4) */

    for (int t = 0; t < 50000; t++) {
        if (!(port->cmd & (1U << 15)) && !(port->cmd & (1U << 14))) {
            return 0;
        }
    }
    return -1;
}

static void ahci_start_port(ahci_port_reg_t *port) {
    while (port->cmd & (1U << 15)); /* Wait until CR is 0 */
    port->cmd |= (1U << 4);         /* Set FRE (bit 4) */
    port->cmd |= (1U << 0);         /* Set ST (bit 0) */
}

/* ── Command Execution ────────────────────────────────────────────────────── */

static int ahci_exec_cmd(ahci_port_t *port, uint8_t cmd, uint64_t lba, uint32_t count, void *buf, bool is_write) {
    if (!port || !port->regs) return -1;
    ahci_port_reg_t *regs = port->regs;

    /* Slot 0 is used for synchronous operations */
    int slot = 0;
    ahci_cmd_header_t *hdr = &port->cmd_headers[slot];
    ahci_cmd_table_t *tbl = &port->cmd_tables[slot];

    memset(tbl, 0, sizeof(ahci_cmd_table_t));

    /* 1. Setup PRDT entry */
    uint64_t phys_buf = (uint64_t)buf;
    uint32_t byte_count = count * AHCI_SECTOR_SIZE;

    tbl->prdt[0].dba = (uint32_t)(phys_buf & 0xFFFFFFFF);
    tbl->prdt[0].dbau = (uint32_t)((phys_buf >> 32) & 0xFFFFFFFF);
    tbl->prdt[0].dbc = byte_count - 1; /* 0-based */
    tbl->prdt[0].i = 1;

    /* 2. Setup Command Header */
    hdr->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t); /* 5 DWORDS */
    hdr->w = is_write ? 1 : 0;
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    /* 3. Setup Command FIS */
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)tbl->cfis;
    memset(fis, 0, sizeof(fis_reg_h2d_t));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1; /* Command */
    fis->command = cmd;
    fis->device = (1 << 6); /* LBA mode */

    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFF);

    fis->count_low = (uint8_t)(count & 0xFF);
    fis->count_high = (uint8_t)((count >> 8) & 0xFF);

    /* 4. Clear interrupt and error registers */
    regs->serr = 0xFFFFFFFF;
    regs->is = 0xFFFFFFFF;

    /* 5. Issue Command */
    regs->ci = (1U << slot);

    /* 6. Wait for completion */
    for (int t = 0; t < 1000000; t++) {
        if (!(regs->ci & (1U << slot))) {
            break;
        }
        if (regs->is & (1U << 30)) { /* Task File Error */
            return -1;
        }
    }

    if (regs->ci & (1U << slot)) {
        return -2; /* Timeout */
    }

    if (regs->is & (1U << 30)) {
        return -1;
    }

    return 0;
}

/* ── Sector I/O Handlers ──────────────────────────────────────────────────── */

static int ahci_port_read(ahci_port_t *port, uint64_t lba, uint32_t count, void *buf) {
    if (!port || !port->present || !buf || count == 0) return -1;

    uint64_t rflags = spinlock_acquire_irqsave(&ahci_lock);
    uint8_t *dest = (uint8_t *)buf;

    while (count > 0) {
        uint32_t batch = (count > 128) ? 128 : count;

        /* Use bounce buffer to guarantee physical memory safety below 128 MB */
        int err = ahci_exec_cmd(port, ATA_CMD_READ_DMA_EXT, lba, batch, ahci_dma_bounce, false);
        if (err != 0) {
            spinlock_release_irqrestore(&ahci_lock, rflags);
            return err;
        }

        memcpy(dest, ahci_dma_bounce, batch * AHCI_SECTOR_SIZE);

        lba += batch;
        count -= batch;
        dest += batch * AHCI_SECTOR_SIZE;
    }

    spinlock_release_irqrestore(&ahci_lock, rflags);
    return 0;
}

static int ahci_port_write(ahci_port_t *port, uint64_t lba, uint32_t count, const void *buf) {
    if (!port || !port->present || !buf || count == 0) return -1;

    uint64_t rflags = spinlock_acquire_irqsave(&ahci_lock);
    const uint8_t *src = (const uint8_t *)buf;

    while (count > 0) {
        uint32_t batch = (count > 128) ? 128 : count;

        memcpy(ahci_dma_bounce, src, batch * AHCI_SECTOR_SIZE);

        int err = ahci_exec_cmd(port, ATA_CMD_WRITE_DMA_EXT, lba, batch, ahci_dma_bounce, true);
        if (err != 0) {
            spinlock_release_irqrestore(&ahci_lock, rflags);
            return err;
        }

        lba += batch;
        count -= batch;
        src += batch * AHCI_SECTOR_SIZE;
    }

    /* Flush cache */
    ahci_exec_cmd(port, ATA_CMD_FLUSH_EXT, 0, 0, ahci_dma_bounce, false);

    spinlock_release_irqrestore(&ahci_lock, rflags);
    return 0;
}

static int ahci_port_flush(ahci_port_t *port) {
    if (!port || !port->present) return -1;
    uint64_t rflags = spinlock_acquire_irqsave(&ahci_lock);
    int res = ahci_exec_cmd(port, ATA_CMD_FLUSH_EXT, 0, 0, ahci_dma_bounce, false);
    spinlock_release_irqrestore(&ahci_lock, rflags);
    return res;
}

/* Wrappers for unified block_device_t */
static int ahci_bdev_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buffer) {
    if (!dev || !dev->private_data) return -1;
    return ahci_port_read((ahci_port_t *)dev->private_data, lba, count, buffer);
}

static int ahci_bdev_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buffer) {
    if (!dev || !dev->private_data) return -1;
    return ahci_port_write((ahci_port_t *)dev->private_data, lba, count, buffer);
}

static int ahci_bdev_flush(block_device_t *dev) {
    if (!dev || !dev->private_data) return -1;
    return ahci_port_flush((ahci_port_t *)dev->private_data);
}

/* ── Port Configuration & Identification ──────────────────────────────────── */

static bool ahci_init_port(ahci_port_t *port, uint8_t port_num, ahci_port_reg_t *regs) {
    port->port_num = port_num;
    port->regs = regs;
    port->present = false;

    ahci_stop_port(regs);

    /* Allocate physically contiguous page frame for headers, FIS, and command table */
    void *dma_page = pmm_alloc_page();
    if (!dma_page) return false;
    memset(dma_page, 0, 4096);

    uint64_t dma_phys = (uint64_t)dma_page;

    /* Layout in page:
     * 0x000..0x3FF: Command List (1024 bytes)
     * 0x400..0x4FF: Received FIS (256 bytes)
     * 0x800..0x9FF: Command Table Slot 0 (512 bytes)
     */
    port->cmd_headers = (ahci_cmd_header_t *)dma_page;
    port->fis_base = (uint8_t *)dma_page + 1024;
    port->cmd_tables = (ahci_cmd_table_t *)((uint8_t *)dma_page + 2048);

    regs->clb = (uint32_t)(dma_phys & 0xFFFFFFFF);
    regs->clbu = (uint32_t)((dma_phys >> 32) & 0xFFFFFFFF);

    regs->fb = (uint32_t)((dma_phys + 1024) & 0xFFFFFFFF);
    regs->fbu = (uint32_t)(((dma_phys + 1024) >> 32) & 0xFFFFFFFF);

    /* Link Command Header 0 to Command Table 0 */
    uint64_t ct_phys = dma_phys + 2048;
    port->cmd_headers[0].ctba = (uint32_t)(ct_phys & 0xFFFFFFFF);
    port->cmd_headers[0].ctbau = (uint32_t)((ct_phys >> 32) & 0xFFFFFFFF);

    regs->serr = 0xFFFFFFFF;
    regs->is = 0xFFFFFFFF;

    ahci_start_port(regs);

    /* Issue ATA IDENTIFY command */
    uint8_t id_buf[512];
    memset(id_buf, 0, sizeof(id_buf));

    if (ahci_exec_cmd(port, ATA_CMD_IDENTIFY_DEV, 0, 1, ahci_dma_bounce, false) != 0) {
        return false;
    }
    memcpy(id_buf, ahci_dma_bounce, 512);

    /* Parse Model String (words 27..46, byte-swapped) */
    uint16_t *id_words = (uint16_t *)id_buf;
    for (int i = 0; i < 20; i++) {
        uint16_t w = id_words[27 + i];
        port->model[i * 2]     = (char)((w >> 8) & 0xFF);
        port->model[i * 2 + 1] = (char)(w & 0xFF);
    }
    port->model[40] = '\0';
    for (int i = 39; i >= 0 && (port->model[i] == ' ' || port->model[i] == '\0'); i--) {
        port->model[i] = '\0';
    }

    /* Total sectors */
    bool lba48 = (id_words[83] & (1 << 10)) != 0;
    if (lba48) {
        memcpy(&port->sectors, &id_words[100], sizeof(uint64_t));
    } else {
        uint32_t s28 = 0;
        memcpy(&s28, &id_words[60], sizeof(uint32_t));
        port->sectors = s28;
    }

    if (port->sectors == 0) {
        uint32_t s28 = 0;
        memcpy(&s28, &id_words[60], sizeof(uint32_t));
        port->sectors = s28;
    }

    port->present = true;

    /* Register as block device */
    char dev_name[16];
    char letter = (char)('a' + ahci_ctrl.port_count);
    dev_name[0] = 's'; dev_name[1] = 'd'; dev_name[2] = letter; dev_name[3] = '\0';

    memset(&port->block_dev, 0, sizeof(block_device_t));
    strncpy(port->block_dev.name, dev_name, BLOCK_DEV_NAME_MAX - 1);
    strncpy(port->block_dev.type_str, "AHCI", sizeof(port->block_dev.type_str) - 1);
    strncpy(port->block_dev.model, port->model[0] ? port->model : "SATA Drive", sizeof(port->block_dev.model) - 1);
    port->block_dev.sector_size = AHCI_SECTOR_SIZE;
    port->block_dev.capacity_sectors = port->sectors;
    port->block_dev.sector_count = port->sectors;
    port->block_dev.read_only = false;
    port->block_dev.present = true;
    port->block_dev.read = ahci_bdev_read;
    port->block_dev.write = ahci_bdev_write;
    port->block_dev.flush = ahci_bdev_flush;
    port->block_dev.private_data = port;

    block_device_register(&port->block_dev);
    ahci_ctrl.port_count++;

    serial_write("[AHCI] Registered /dev/");
    serial_write(dev_name);
    serial_write(" (Model: ");
    serial_write(port->block_dev.model);
    serial_write(", Sectors: ");
    serial_write_dec(port->sectors);
    serial_write(")\n");

    return true;
}

/* ── Controller Initialization ────────────────────────────────────────────── */

int ahci_init(void) {
    memset(&ahci_ctrl, 0, sizeof(ahci_controller_t));

    /* 1. Allocate 64 KiB contiguous bounce buffer */
    if (!ahci_dma_bounce) {
        ahci_dma_bounce = (uint8_t *)pmm_alloc_pages(16);
        if (!ahci_dma_bounce) return -1;
    }

    /* 2. Locate AHCI controller via PCI */
    pci_device_t *pci = pci_find_storage(PCI_SUBCLASS_STORAGE_SATA, PCI_PROG_IF_SATA_AHCI);
    if (!pci) {
        return 0; /* No AHCI controller present on system */
    }

    ahci_ctrl.pci_dev = pci;

    /* 3. Enable Bus Mastering and Memory Space */
    pci_enable_bus_master(pci);
    pci_enable_mem_space(pci);

    /* 4. Map BAR5 MMIO space (at least 8 pages = 32 KiB) */
    uint64_t bar5_phys = pci->bar[5] & ~0xFULL;
    if (!bar5_phys) {
        /* Fallback: If BAR5 was unassigned by BIOS, allocate in PCI MMIO window */
        bar5_phys = 0xFEBE0000;
        pci_write_config_dword(pci->bus, pci->slot, pci->func, PCI_REG_BAR5, (uint32_t)bar5_phys);
        pci->bar[5] = bar5_phys;
        pci->bar_size[5] = 4096;
    }

    for (uint64_t p = 0; p < 8; p++) {
        uint64_t pa = bar5_phys + p * 4096;
        paging_map_page(pa, pa, PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE);
    }

    ahci_ctrl.abar = (ahci_hba_memory_t *)bar5_phys;
    ahci_hba_memory_t *abar = ahci_ctrl.abar;

    /* 5. BIOS Handoff (if supported) */
    if (abar->cap2 & 1) {
        abar->bohc |= (1U << 1); /* Request OS ownership */
        for (int t = 0; t < 25000; t++) {
            if (!(abar->bohc & 1)) break;
        }
    }

    /* 6. Enable AHCI Mode and Reset Controller */
    abar->ghc |= (1U << 31); /* Set AE (AHCI Enable) */
    abar->ghc |= (1U << 0);  /* Trigger HR (HBA Reset) */

    for (int t = 0; t < 50000; t++) {
        if (!(abar->ghc & 1)) break;
    }

    abar->ghc |= (1U << 31); /* Re-enable AE */

    /* 7. Discover and Initialize Implemented SATA Ports */
    uint32_t pi = abar->pi;
    for (uint8_t i = 0; i < AHCI_MAX_PORTS; i++) {
        if (pi & (1U << i)) {
            ahci_port_reg_t *port_regs = &abar->ports[i];

            /* Spin up and power on device */
            port_regs->cmd |= (1U << 1); /* POD */
            port_regs->cmd |= (1U << 2); /* SUD */

            for (volatile int d = 0; d < 50000; d++);

            uint32_t ssts = port_regs->ssts;
            uint8_t det = (uint8_t)(ssts & 0x0F);

            if (det == HBA_PORT_DET_PRESENT) {
                ahci_init_port(&ahci_ctrl.ports[i], i, port_regs);
            }
        }
    }

    ahci_ctrl.initialized = true;

    if (ahci_ctrl.port_count > 0) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("[ OK ]  ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_print("AHCI SATA (");
        vga_print_dec((uint64_t)ahci_ctrl.port_count);
        vga_println(" drives online)");
    }

    return (int)ahci_ctrl.port_count;
}

bool ahci_is_present(void) {
    return ahci_ctrl.initialized && (ahci_ctrl.port_count > 0);
}
