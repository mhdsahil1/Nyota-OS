/* =============================================================================
 * Nyota OS — ATA PIO Storage Driver Implementation (Phase 6)
 * Detects ATA IDE drives, reads disk geometry, and performs PIO sector I/O.
 * =========================================================================== */

#include "storage/ata.h"
#include "storage/block.h"
#include "io.h"
#include "memory.h"
#include "vga.h"
#include "kernel.h"

static ata_device_t ata_drives[2]; /* Drive 0: Primary Master, Drive 1: Primary Slave */

/* 400ns delay by reading alternate status register 4 times */
static inline void ata_delay(uint16_t ctrl_base) {
    inb(ctrl_base);
    inb(ctrl_base);
    inb(ctrl_base);
    inb(ctrl_base);
}

/* Poll status register until BSY clears, check for error */
static int ata_wait_ready(uint16_t io_base, uint16_t ctrl_base) {
    ata_delay(ctrl_base);
    for (int timeout = 0; timeout < 100000; timeout++) {
        uint8_t status = inb(io_base + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) {
            if (status & (ATA_SR_ERR | ATA_SR_DF)) {
                return -1;
            }
            return 0;
        }
    }
    return -2; /* Timeout */
}

/* Wait until drive is ready to transfer data (DRQ set and BSY clear) */
static int ata_wait_drq(uint16_t io_base, uint16_t ctrl_base) {
    ata_delay(ctrl_base);
    for (int timeout = 0; timeout < 100000; timeout++) {
        uint8_t status = inb(io_base + ATA_REG_STATUS);
        if (status & (ATA_SR_ERR | ATA_SR_DF)) {
            return -1;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) {
            return 0;
        }
    }
    return -2; /* Timeout */
}

/* Read sectors using LBA28 PIO */
static int ata_read_lba28(ata_device_t *dev, uint32_t lba, uint8_t count, void *buf) {
    uint16_t io = dev->io_base;
    uint16_t ctrl = dev->ctrl_base;

    if (ata_wait_ready(io, ctrl) != 0) return -1;

    /* Select drive and top 4 bits of LBA */
    outb(io + ATA_REG_HDDEVSEL, (dev->drive == ATA_MASTER ? 0xE0 : 0xF0) | ((lba >> 24) & 0x0F));
    ata_delay(ctrl);

    outb(io + ATA_REG_SECCOUNT0, count);
    outb(io + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(io + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(io + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(io + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    uint16_t *target = (uint16_t *)buf;
    uint32_t num_sectors = (count == 0) ? 256 : count;

    for (uint32_t s = 0; s < num_sectors; s++) {
        if (ata_wait_drq(io, ctrl) != 0) {
            return -1;
        }
        for (int w = 0; w < 256; w++) {
            *target++ = inw(io + ATA_REG_DATA);
        }
    }

    return 0;
}

/* Write sectors using LBA28 PIO */
static int ata_write_lba28(ata_device_t *dev, uint32_t lba, uint8_t count, const void *buf) {
    uint16_t io = dev->io_base;
    uint16_t ctrl = dev->ctrl_base;

    if (ata_wait_ready(io, ctrl) != 0) return -1;

    outb(io + ATA_REG_HDDEVSEL, (dev->drive == ATA_MASTER ? 0xE0 : 0xF0) | ((lba >> 24) & 0x0F));
    ata_delay(ctrl);

    outb(io + ATA_REG_SECCOUNT0, count);
    outb(io + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(io + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(io + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(io + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    const uint16_t *src = (const uint16_t *)buf;
    uint32_t num_sectors = (count == 0) ? 256 : count;

    for (uint32_t s = 0; s < num_sectors; s++) {
        if (ata_wait_drq(io, ctrl) != 0) {
            return -1;
        }
        for (int w = 0; w < 256; w++) {
            outw(io + ATA_REG_DATA, *src++);
        }
    }

    /* Flush cache */
    outb(io + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_ready(io, ctrl);

    return 0;
}

int ata_read_sectors(ata_device_t *dev, uint64_t lba, uint32_t count, void *buf) {
    if (!dev || !dev->present || !buf || count == 0) return -1;

    uint8_t *ptr = (uint8_t *)buf;
    while (count > 0) {
        uint32_t batch = (count > 128) ? 128 : count;
        if (ata_read_lba28(dev, (uint32_t)lba, (uint8_t)batch, ptr) != 0) {
            return -1;
        }
        lba += batch;
        count -= batch;
        ptr += batch * ATA_SECTOR_SIZE;
    }
    return 0;
}

int ata_write_sectors(ata_device_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    if (!dev || !dev->present || !buf || count == 0) return -1;

    const uint8_t *ptr = (const uint8_t *)buf;
    while (count > 0) {
        uint32_t batch = (count > 128) ? 128 : count;
        if (ata_write_lba28(dev, (uint32_t)lba, (uint8_t)batch, ptr) != 0) {
            return -1;
        }
        lba += batch;
        count -= batch;
        ptr += batch * ATA_SECTOR_SIZE;
    }
    return 0;
}

/* Wrappers for block_device_t interface */
static int ata0_read_block(uint64_t sector, uint32_t count, void *buffer) {
    return ata_read_sectors(&ata_drives[0], sector, count, buffer);
}

static int ata0_write_block(uint64_t sector, uint32_t count, const void *buffer) {
    return ata_write_sectors(&ata_drives[0], sector, count, buffer);
}

static int ata1_read_block(uint64_t sector, uint32_t count, void *buffer) {
    return ata_read_sectors(&ata_drives[1], sector, count, buffer);
}

static int ata1_write_block(uint64_t sector, uint32_t count, const void *buffer) {
    return ata_write_sectors(&ata_drives[1], sector, count, buffer);
}

/* Identify ATA Drive */
static bool ata_identify(ata_device_t *dev) {
    uint16_t io = dev->io_base;
    uint16_t ctrl = dev->ctrl_base;

    /* Select drive */
    outb(io + ATA_REG_HDDEVSEL, dev->drive == ATA_MASTER ? 0xA0 : 0xB0);
    ata_delay(ctrl);

    /* Zero cylinder and sector count registers */
    outb(io + ATA_REG_SECCOUNT0, 0);
    outb(io + ATA_REG_LBA0, 0);
    outb(io + ATA_REG_LBA1, 0);
    outb(io + ATA_REG_LBA2, 0);

    /* Send IDENTIFY command */
    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay(ctrl);

    uint8_t status = inb(io + ATA_REG_STATUS);
    if (status == 0 || status == 0xFF) {
        /* No drive */
        return false;
    }

    /* Wait for BSY to clear */
    while (inb(io + ATA_REG_STATUS) & ATA_SR_BSY) {
        /* Busy wait */
    }

    /* Check for ATAPI */
    uint8_t mid = inb(io + ATA_REG_LBA1);
    uint8_t hi  = inb(io + ATA_REG_LBA2);
    if (mid != 0 || hi != 0) {
        /* ATAPI device, skip */
        return false;
    }

    /* Wait until DRQ or ERR */
    for (int timeout = 0; timeout < 50000; timeout++) {
        status = inb(io + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) {
            return false;
        }
        if (status & ATA_SR_DRQ) {
            break;
        }
    }

    if (!(inb(io + ATA_REG_STATUS) & ATA_SR_DRQ)) {
        return false;
    }

    /* Read 256 words of IDENTIFY data */
    uint16_t identify_buf[256];
    memset(identify_buf, 0, sizeof(identify_buf));
    for (int i = 0; i < 256; i++) {
        identify_buf[i] = inw(io + ATA_REG_DATA);
    }

    /* Extract model string (words 27-46, byte-swapped) */
    for (int i = 0; i < 20; i++) {
        uint16_t w = identify_buf[27 + i];
        dev->model[i * 2]     = (char)((w >> 8) & 0xFF);
        dev->model[i * 2 + 1] = (char)(w & 0xFF);
    }
    dev->model[40] = '\0';
    /* Trim trailing spaces */
    for (int i = 39; i >= 0 && dev->model[i] == ' '; i--) {
        dev->model[i] = '\0';
    }

    /* Total sectors */
    dev->lba48_supported = (identify_buf[83] & (1 << 10)) != 0;
    if (dev->lba48_supported) {
        memcpy(&dev->sectors, &identify_buf[100], sizeof(uint64_t));
    } else {
        uint32_t s28 = 0;
        memcpy(&s28, &identify_buf[60], sizeof(uint32_t));
        dev->sectors = s28;
    }

    if (dev->sectors == 0) {
        uint32_t s28 = 0;
        memcpy(&s28, &identify_buf[60], sizeof(uint32_t));
        dev->sectors = s28;
    }

    dev->present = true;
    return true;
}

void ata_init(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("[INFO]  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("Initializing storage");

    block_device_init();

    /* Disable interrupts on primary IDE controller (use PIO polling) */
    outb(ATA_PRIMARY_CTRL_BASE, 0x02);

    /* Test controller presence */
    uint8_t status = inb(ATA_PRIMARY_IO_BASE + ATA_REG_STATUS);
    if (status == 0xFF) {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_println("[WARN]  ATA controller floating / not present");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        return;
    }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ]  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("ATA controller detected");

    /* Initialize drive structs */
    ata_drives[0].io_base = ATA_PRIMARY_IO_BASE;
    ata_drives[0].ctrl_base = ATA_PRIMARY_CTRL_BASE;
    ata_drives[0].drive = ATA_MASTER;
    ata_drives[0].present = false;

    ata_drives[1].io_base = ATA_PRIMARY_IO_BASE;
    ata_drives[1].ctrl_base = ATA_PRIMARY_CTRL_BASE;
    ata_drives[1].drive = ATA_SLAVE;
    ata_drives[1].present = false;

    /* Probe Primary Master (ata0) */
    if (ata_identify(&ata_drives[0])) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("[ OK ]  ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_print("Disk detected (Primary Master: ");
        vga_print(ata_drives[0].model[0] ? ata_drives[0].model : "QEMU ATA");
        vga_println(")");

        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_print("[INFO]  ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_println("Sector size: 512");

        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_print("[INFO]  ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_print("Disk sectors: ");
        vga_print_dec(ata_drives[0].sectors);
        vga_println("");

        /* Register block device */
        memcpy(ata_drives[0].block_dev.name, "ata0", 5);
        ata_drives[0].block_dev.sector_size = ATA_SECTOR_SIZE;
        ata_drives[0].block_dev.sector_count = ata_drives[0].sectors;
        ata_drives[0].block_dev.read = ata0_read_block;
        ata_drives[0].block_dev.write = ata0_write_block;
        ata_drives[0].block_dev.private_data = &ata_drives[0];
        block_device_register(&ata_drives[0].block_dev);
    }

    /* Probe Primary Slave (ata1) */
    if (ata_identify(&ata_drives[1])) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("[ OK ]  ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_print("Disk detected (Primary Slave: ");
        vga_print(ata_drives[1].model[0] ? ata_drives[1].model : "NyotaFS Disk");
        vga_println(")");

        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_print("[INFO]  ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_println("Sector size: 512");

        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_print("[INFO]  ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_print("Disk sectors: ");
        vga_print_dec(ata_drives[1].sectors);
        vga_println("");

        /* Register block device */
        memcpy(ata_drives[1].block_dev.name, "ata1", 5);
        ata_drives[1].block_dev.sector_size = ATA_SECTOR_SIZE;
        ata_drives[1].block_dev.sector_count = ata_drives[1].sectors;
        ata_drives[1].block_dev.read = ata1_read_block;
        ata_drives[1].block_dev.write = ata1_write_block;
        ata_drives[1].block_dev.private_data = &ata_drives[1];
        block_device_register(&ata_drives[1].block_dev);
    }
}
