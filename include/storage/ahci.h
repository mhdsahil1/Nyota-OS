/* =============================================================================
 * Nyota OS — Advanced Host Controller Interface (AHCI / SATA) Driver Header
 * Implements SATA controller discovery, MMIO configuration, and DMA sector I/O.
 * =========================================================================== */

#ifndef NYOTA_STORAGE_AHCI_H
#define NYOTA_STORAGE_AHCI_H

#include "types.h"
#include "storage/block.h"
#include "drivers/pci.h"

#define AHCI_MAX_PORTS          32
#define AHCI_CMD_SLOTS          32
#define AHCI_SECTOR_SIZE        512

/* FIS Types */
#define FIS_TYPE_REG_H2D        0x27
#define FIS_TYPE_REG_D2H        0x34
#define FIS_TYPE_DMA_ACT        0x39
#define FIS_TYPE_DMA_SETUP      0x41
#define FIS_TYPE_DATA           0x46
#define FIS_TYPE_BIST           0x58
#define FIS_TYPE_PIO_SETUP      0x5F
#define FIS_TYPE_DEV_BITS       0xA1

/* ATA Commands for SATA */
#define ATA_CMD_READ_DMA_EXT    0x25
#define ATA_CMD_WRITE_DMA_EXT   0x35
#define ATA_CMD_FLUSH_EXT       0xEA
#define ATA_CMD_IDENTIFY_DEV    0xEC

/* Port Signatures */
#define SATA_SIG_ATA            0x00000101
#define SATA_SIG_ATAPI          0xEB140101
#define SATA_SIG_SEMB           0xC33C0101
#define SATA_SIG_PM             0x96690101

/* Port Status DET field */
#define HBA_PORT_DET_PRESENT    0x03
#define HBA_PORT_IPM_ACTIVE     0x01

/* Command Header */
typedef struct __attribute__((packed)) {
    uint8_t  cfl:5;             /* Command FIS length in DWORDS */
    uint8_t  a:1;               /* ATAPI */
    uint8_t  w:1;               /* Write (1 = Host->Device, 0 = Device->Host) */
    uint8_t  p:1;               /* Prefetchable */
    uint8_t  r:1;               /* Reset */
    uint8_t  b:1;               /* BIST */
    uint8_t  c:1;               /* Clear busy upon R_OK */
    uint8_t  reserved0:1;
    uint8_t  pmp:4;             /* Port multiplier port */
    uint16_t prdtl;             /* PRDT entry count */
    volatile uint32_t prdbc;    /* PRD byte count transferred */
    uint32_t ctba;              /* Command table base address (low 32) */
    uint32_t ctbau;             /* Command table base address (high 32) */
    uint32_t reserved1[4];
} ahci_cmd_header_t;

/* PRDT Entry */
typedef struct __attribute__((packed)) {
    uint32_t dba;               /* Data Base Address low 32 */
    uint32_t dbau;              /* Data Base Address high 32 */
    uint32_t reserved0;
    uint32_t dbc:22;            /* Byte count (0-based: byte_count - 1) */
    uint32_t reserved1:9;
    uint32_t i:1;               /* Interrupt on completion */
} ahci_prdt_entry_t;

/* Command Table */
typedef struct __attribute__((packed)) {
    uint8_t  cfis[64];          /* Command FIS */
    uint8_t  acmd[16];          /* ATAPI command */
    uint8_t  reserved[48];
    ahci_prdt_entry_t prdt[16]; /* Up to 16 PRDT entries (64 KiB transfer) */
} ahci_cmd_table_t;

/* Host to Device Register FIS */
typedef struct __attribute__((packed)) {
    uint8_t  fis_type;          /* 0x27 */
    uint8_t  pmport:4;
    uint8_t  reserved0:3;
    uint8_t  c:1;               /* 1 = Command */
    uint8_t  command;           /* ATA Command */
    uint8_t  feature_low;
    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;            /* (1 << 6) for LBA mode */
    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  feature_high;
    uint8_t  count_low;
    uint8_t  count_high;
    uint8_t  icc;
    uint8_t  control;
    uint8_t  reserved1[4];
} fis_reg_h2d_t;

/* AHCI Port MMIO Registers */
typedef struct __attribute__((packed)) {
    volatile uint32_t clb;      /* 0x00: Command List Base low */
    volatile uint32_t clbu;     /* 0x04: Command List Base high */
    volatile uint32_t fb;       /* 0x08: FIS Base low */
    volatile uint32_t fbu;      /* 0x0C: FIS Base high */
    volatile uint32_t is;       /* 0x10: Interrupt Status */
    volatile uint32_t ie;       /* 0x14: Interrupt Enable */
    volatile uint32_t cmd;      /* 0x18: Command and Status */
    volatile uint32_t reserved0;/* 0x1C */
    volatile uint32_t tfd;      /* 0x20: Task File Data */
    volatile uint32_t sig;      /* 0x24: Signature */
    volatile uint32_t ssts;     /* 0x28: SATA Status (SStatus) */
    volatile uint32_t sctl;     /* 0x2C: SATA Control (SControl) */
    volatile uint32_t serr;     /* 0x30: SATA Error (SError) */
    volatile uint32_t sact;     /* 0x34: SATA Active (SActive) */
    volatile uint32_t ci;       /* 0x38: Command Issue */
    volatile uint32_t sntf;     /* 0x3C: SNotification */
    volatile uint32_t fbs;      /* 0x40: FIS-based Switching */
    volatile uint32_t reserved1[11];
    volatile uint32_t vendor[4];
} ahci_port_reg_t;

/* AHCI Generic Host Control MMIO Registers */
typedef struct __attribute__((packed)) {
    volatile uint32_t cap;      /* 0x00: Host Capabilities */
    volatile uint32_t ghc;      /* 0x04: Global Host Control */
    volatile uint32_t is;       /* 0x08: Interrupt Status */
    volatile uint32_t pi;       /* 0x0C: Ports Implemented */
    volatile uint32_t vs;       /* 0x10: Version */
    volatile uint32_t ccc_ctl;  /* 0x14: Command Completion Coalescing Control */
    volatile uint32_t ccc_pts;  /* 0x18: CCC Ports */
    volatile uint32_t em_loc;   /* 0x1C: Enclosure Management Location */
    volatile uint32_t em_ctl;   /* 0x20: Enclosure Management Control */
    volatile uint32_t cap2;     /* 0x24: Host Capabilities Extended */
    volatile uint32_t bohc;     /* 0x28: BIOS/OS Handoff Control and Status */
    uint8_t           reserved[0x74]; /* 0x2C..0x9F */
    uint8_t           vendor[0x60];   /* 0xA0..0xFF */
    ahci_port_reg_t   ports[32];      /* 0x100..0x10FF */
} ahci_hba_memory_t;

/* AHCI Port Driver State */
typedef struct {
    uint8_t           port_num;
    bool              present;
    ahci_port_reg_t  *regs;
    ahci_cmd_header_t*cmd_headers;
    ahci_cmd_table_t *cmd_tables;
    uint8_t          *fis_base;
    uint64_t          sectors;
    char              model[41];
    block_device_t    block_dev;
} ahci_port_t;

/* AHCI Controller State */
typedef struct {
    pci_device_t     *pci_dev;
    ahci_hba_memory_t*abar;
    bool              initialized;
    uint32_t          port_count;
    ahci_port_t       ports[AHCI_MAX_PORTS];
} ahci_controller_t;

/* AHCI APIs */
int  ahci_init(void);
bool ahci_is_present(void);

#endif /* NYOTA_STORAGE_AHCI_H */
