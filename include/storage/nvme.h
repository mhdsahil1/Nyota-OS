/* =============================================================================
 * Nyota OS — Non-Volatile Memory Express (NVMe) Storage Driver Header
 * Implements NVMe controller initialization, Admin/IO queues, and DMA block I/O.
 * =========================================================================== */

#ifndef NYOTA_STORAGE_NVME_H
#define NYOTA_STORAGE_NVME_H

#include "types.h"
#include "storage/block.h"
#include "drivers/pci.h"

#define NVME_ADMIN_QUEUE_ENTRIES 64
#define NVME_IO_QUEUE_ENTRIES    64
#define NVME_DEFAULT_SECTOR_SIZE 512

/* NVMe Admin Opcodes */
#define NVME_ADMIN_OP_DELETE_IO_SQ  0x00
#define NVME_ADMIN_OP_CREATE_IO_SQ  0x01
#define NVME_ADMIN_OP_DELETE_IO_CQ  0x04
#define NVME_ADMIN_OP_CREATE_IO_CQ  0x05
#define NVME_ADMIN_OP_IDENTIFY      0x06

/* NVMe I/O Opcodes */
#define NVME_IO_OP_FLUSH            0x00
#define NVME_IO_OP_WRITE            0x01
#define NVME_IO_OP_READ             0x02

/* NVMe MMIO Registers */
#define NVME_REG_CAP                0x00   /* 64-bit: Controller Capabilities */
#define NVME_REG_VS                 0x08   /* 32-bit: Version */
#define NVME_REG_INTMS              0x0C   /* 32-bit: Interrupt Mask Set */
#define NVME_REG_INTMC              0x10   /* 32-bit: Interrupt Mask Clear */
#define NVME_REG_CC                 0x14   /* 32-bit: Controller Configuration */
#define NVME_REG_CSTS               0x1C   /* 32-bit: Controller Status */
#define NVME_REG_AQA                0x24   /* 32-bit: Admin Queue Attributes */
#define NVME_REG_ASQ                0x28   /* 64-bit: Admin Submission Queue Base */
#define NVME_REG_ACQ                0x30   /* 64-bit: Admin Completion Queue Base */

/* NVMe Submission Queue Entry (64 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t reserved0;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_cmd_t;

/* NVMe Completion Queue Entry (16 bytes) */
typedef struct __attribute__((packed)) {
    uint32_t result;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} nvme_cqe_t;

/* NVMe Controller & Queue State */
typedef struct {
    pci_device_t       *pci_dev;
    volatile uint8_t   *mmio;
    bool                initialized;

    uint32_t            doorbell_stride;
    uint32_t            sector_size;
    uint64_t            capacity_sectors;
    char                model[41];
    char                serial[21];

    /* Admin Queue (Queue 0) */
    nvme_cmd_t         *admin_sq;
    nvme_cqe_t         *admin_cq;
    uint16_t            admin_sq_tail;
    uint16_t            admin_cq_head;
    uint8_t             admin_cq_phase;
    uint16_t            admin_cid;

    /* I/O Queue (Queue 1) */
    nvme_cmd_t         *io_sq;
    nvme_cqe_t         *io_cq;
    uint16_t            io_sq_tail;
    uint16_t            io_cq_head;
    uint8_t             io_cq_phase;
    uint16_t            io_cid;

    block_device_t      block_dev;
} nvme_controller_t;

/* NVMe APIs */
int  nvme_init(void);
bool nvme_is_present(void);

#endif /* NYOTA_STORAGE_NVME_H */
