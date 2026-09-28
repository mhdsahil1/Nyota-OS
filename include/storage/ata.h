/* =============================================================================
 * Nyota OS — ATA PIO Storage Driver Header (Phase 6)
 * PIO mode sector read/write for IDE/ATA controllers.
 * =========================================================================== */

#ifndef NYOTA_STORAGE_ATA_H
#define NYOTA_STORAGE_ATA_H

#include "types.h"
#include "storage/block.h"

#define ATA_SECTOR_SIZE         512

/* ATA Primary Bus I/O Ports */
#define ATA_PRIMARY_IO_BASE     0x1F0
#define ATA_PRIMARY_CTRL_BASE   0x3F6

/* ATA Secondary Bus I/O Ports */
#define ATA_SECONDARY_IO_BASE   0x170
#define ATA_SECONDARY_CTRL_BASE 0x376

/* ATA Port Offsets */
#define ATA_REG_DATA            0x00
#define ATA_REG_ERROR           0x01
#define ATA_REG_FEATURES        0x01
#define ATA_REG_SECCOUNT0       0x02
#define ATA_REG_LBA0            0x03
#define ATA_REG_LBA1            0x04
#define ATA_REG_LBA2            0x05
#define ATA_REG_HDDEVSEL        0x06
#define ATA_REG_COMMAND         0x07
#define ATA_REG_STATUS          0x07

/* Status Register Bits */
#define ATA_SR_BSY              0x80  /* Busy */
#define ATA_SR_DRDY             0x40  /* Drive Ready */
#define ATA_SR_DF               0x20  /* Drive Fault */
#define ATA_SR_DSC              0x10  /* Drive Seek Complete */
#define ATA_SR_DRQ              0x08  /* Data Request Ready */
#define ATA_SR_CORR             0x04  /* Corrected Data */
#define ATA_SR_IDX              0x02  /* Index */
#define ATA_SR_ERR              0x01  /* Error */

/* ATA Commands */
#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_CACHE_FLUSH     0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA
#define ATA_CMD_IDENTIFY        0xEC

/* ATA Drive Select */
#define ATA_MASTER              0x00
#define ATA_SLAVE               0x01

/* ATA Device Structure */
typedef struct ata_device {
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t  drive;           /* ATA_MASTER or ATA_SLAVE */
    bool     present;
    bool     lba48_supported;
    uint64_t sectors;
    char     model[41];
    block_device_t block_dev;
} ata_device_t;

/* APIs */
void ata_init(void);
int ata_read_sectors(ata_device_t *dev, uint64_t lba, uint32_t count, void *buf);
int ata_write_sectors(ata_device_t *dev, uint64_t lba, uint32_t count, const void *buf);

#endif /* NYOTA_STORAGE_ATA_H */
