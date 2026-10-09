/* =============================================================================
 * Nyota OS — Non-Volatile Memory Express (NVMe) Storage Driver
 * Detects NVMe controllers via PCI, resets controller, initializes Admin and
 * I/O submission/completion queues, executes DMA sector I/O, and registers "nvme0".
 * =========================================================================== */

#include "storage/nvme.h"
#include "storage/block.h"
#include "drivers/pci.h"
#include "paging.h"
#include "pmm.h"
#include "memory.h"
#include "vga.h"
#include "serial.h"
#include "kernel.h"
#include "spinlock.h"

static nvme_controller_t nvme_ctrl;
static spinlock_t nvme_lock = SPINLOCK_INIT;
static uint8_t *nvme_dma_bounce = NULL;

/* ── Low-Level Register Access ────────────────────────────────────────────── */

static inline uint32_t nvme_read32(uint32_t offset) {
    uint32_t val;
    __asm__ volatile ("movl (%1), %0" : "=r"(val) : "r"(nvme_ctrl.mmio + offset) : "memory");
    return val;
}

static inline void nvme_write32(uint32_t offset, uint32_t val) {
    __asm__ volatile ("movl %0, (%1)" :: "r"(val), "r"(nvme_ctrl.mmio + offset) : "memory");
}

static inline uint64_t nvme_read64(uint32_t offset) {
    uint64_t val;
    __asm__ volatile ("movq (%1), %0" : "=r"(val) : "r"(nvme_ctrl.mmio + offset) : "memory");
    return val;
}

static inline void nvme_write64(uint32_t offset, uint64_t val) {
    __asm__ volatile ("movq %0, (%1)" :: "r"(val), "r"(nvme_ctrl.mmio + offset) : "memory");
}

/* Doorbell Registers */
static inline void nvme_ring_sq(uint16_t qid, uint16_t tail) {
    uint32_t stride = 4 << nvme_ctrl.doorbell_stride;
    uint32_t db_offset = 0x1000 + (2 * qid) * stride;
    nvme_write32(db_offset, tail);
}

static inline void nvme_ring_cq(uint16_t qid, uint16_t head) {
    uint32_t stride = 4 << nvme_ctrl.doorbell_stride;
    uint32_t db_offset = 0x1000 + (2 * qid + 1) * stride;
    nvme_write32(db_offset, head);
}

/* ── Admin Command Submission & Completion ────────────────────────────────── */

static int nvme_submit_admin_cmd(nvme_cmd_t *cmd, nvme_cqe_t *out_cqe) {
    uint16_t cid = ++nvme_ctrl.admin_cid;
    cmd->cid = cid;

    /* 1. Place command in Admin SQ */
    uint16_t tail = nvme_ctrl.admin_sq_tail;
    memcpy(&nvme_ctrl.admin_sq[tail], cmd, sizeof(nvme_cmd_t));

    tail = (tail + 1) % NVME_ADMIN_QUEUE_ENTRIES;
    nvme_ctrl.admin_sq_tail = tail;

    /* 2. Ring Admin SQ Doorbell (Queue 0) */
    nvme_ring_sq(0, tail);

    /* 3. Poll Admin CQ for completion */
    uint16_t head = nvme_ctrl.admin_cq_head;
    volatile nvme_cqe_t *cqe = (volatile nvme_cqe_t *)&nvme_ctrl.admin_cq[head];

    for (int t = 0; t < 1000000; t++) {
        uint16_t status = cqe->status;
        if ((status & 1) == nvme_ctrl.admin_cq_phase) {
            break;
        }
        __asm__ volatile ("pause" ::: "memory");
    }

    uint16_t status = cqe->status;
    if ((status & 1) != nvme_ctrl.admin_cq_phase) {
        return -2; /* Timeout */
    }

    if (out_cqe) {
        memcpy(out_cqe, cqe, sizeof(nvme_cqe_t));
    }

    uint16_t sc = (cqe->status >> 1) & 0x7FF;

    /* Advance CQ Head */
    head = (head + 1) % NVME_ADMIN_QUEUE_ENTRIES;
    if (head == 0) {
        nvme_ctrl.admin_cq_phase ^= 1;
    }
    nvme_ctrl.admin_cq_head = head;

    /* 4. Ring Admin CQ Doorbell (Queue 0) */
    nvme_ring_cq(0, head);

    return (sc == 0) ? 0 : -1;
}

/* ── I/O Command Submission & Completion ──────────────────────────────────── */

static int nvme_submit_io_cmd(nvme_cmd_t *cmd, nvme_cqe_t *out_cqe) {
    uint16_t cid = ++nvme_ctrl.io_cid;
    cmd->cid = cid;

    uint16_t tail = nvme_ctrl.io_sq_tail;
    memcpy(&nvme_ctrl.io_sq[tail], cmd, sizeof(nvme_cmd_t));

    tail = (tail + 1) % NVME_IO_QUEUE_ENTRIES;
    nvme_ctrl.io_sq_tail = tail;

    /* Ring I/O SQ Doorbell (Queue 1) */
    nvme_ring_sq(1, tail);

    /* Poll I/O CQ (Queue 1) */
    uint16_t head = nvme_ctrl.io_cq_head;
    volatile nvme_cqe_t *cqe = (volatile nvme_cqe_t *)&nvme_ctrl.io_cq[head];

    for (int t = 0; t < 1000000; t++) {
        uint16_t status = cqe->status;
        if ((status & 1) == nvme_ctrl.io_cq_phase) {
            break;
        }
        __asm__ volatile ("pause" ::: "memory");
    }

    uint16_t status = cqe->status;
    if ((status & 1) != nvme_ctrl.io_cq_phase) {
        return -2; /* Timeout */
    }

    if (out_cqe) {
        memcpy(out_cqe, cqe, sizeof(nvme_cqe_t));
    }

    uint16_t sc = (cqe->status >> 1) & 0x7FF;

    head = (head + 1) % NVME_IO_QUEUE_ENTRIES;
    if (head == 0) {
        nvme_ctrl.io_cq_phase ^= 1;
    }
    nvme_ctrl.io_cq_head = head;

    nvme_ring_cq(1, head);

    return (sc == 0) ? 0 : -1;
}

/* ── Sector I/O Handlers ──────────────────────────────────────────────────── */

static int nvme_exec_io(uint8_t opcode, uint64_t lba, uint32_t count, void *buf) {
    uint8_t *dest = (uint8_t *)buf;

    while (count > 0) {
        /* Max batch: 128 sectors (64 KiB), fits within bounce buffer */
        uint32_t batch = (count > 128) ? 128 : count;

        if (opcode == NVME_IO_OP_WRITE) {
            memcpy(nvme_dma_bounce, dest, batch * nvme_ctrl.sector_size);
        }

        nvme_cmd_t cmd;
        memset(&cmd, 0, sizeof(nvme_cmd_t));
        cmd.opcode = opcode;
        cmd.nsid = 1;

        uint64_t phys_buf = (uint64_t)nvme_dma_bounce;
        cmd.prp1 = phys_buf;

        /* If buffer crosses a 4 KiB boundary, supply PRP2 */
        if (batch * nvme_ctrl.sector_size > 4096) {
            cmd.prp2 = phys_buf + 4096;
        }

        cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
        cmd.cdw11 = (uint32_t)((lba >> 32) & 0xFFFFFFFF);
        cmd.cdw12 = (batch - 1) & 0xFFFF; /* 0-based block count */

        int err = nvme_submit_io_cmd(&cmd, NULL);
        if (err != 0) {
            return err;
        }

        if (opcode == NVME_IO_OP_READ) {
            memcpy(dest, nvme_dma_bounce, batch * nvme_ctrl.sector_size);
        }

        lba += batch;
        count -= batch;
        dest += batch * nvme_ctrl.sector_size;
    }

    return 0;
}

static int nvme_read_sectors(uint64_t lba, uint32_t count, void *buf) {
    if (!nvme_ctrl.initialized || !buf || count == 0) return -1;
    uint64_t rflags = spinlock_acquire_irqsave(&nvme_lock);
    int res = nvme_exec_io(NVME_IO_OP_READ, lba, count, buf);
    spinlock_release_irqrestore(&nvme_lock, rflags);
    return res;
}

static int nvme_write_sectors(uint64_t lba, uint32_t count, const void *buf) {
    if (!nvme_ctrl.initialized || !buf || count == 0) return -1;
    uint64_t rflags = spinlock_acquire_irqsave(&nvme_lock);
    int res = nvme_exec_io(NVME_IO_OP_WRITE, lba, count, (void *)buf);
    spinlock_release_irqrestore(&nvme_lock, rflags);
    return res;
}

static int nvme_flush_cache(void) {
    if (!nvme_ctrl.initialized) return -1;
    uint64_t rflags = spinlock_acquire_irqsave(&nvme_lock);

    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(nvme_cmd_t));
    cmd.opcode = NVME_IO_OP_FLUSH;
    cmd.nsid = 1;

    int res = nvme_submit_io_cmd(&cmd, NULL);
    spinlock_release_irqrestore(&nvme_lock, rflags);
    return res;
}

/* Block device wrappers */
static int nvme_bdev_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buffer) {
    (void)dev;
    return nvme_read_sectors(lba, count, buffer);
}

static int nvme_bdev_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buffer) {
    (void)dev;
    return nvme_write_sectors(lba, count, buffer);
}

static int nvme_bdev_flush(block_device_t *dev) {
    (void)dev;
    return nvme_flush_cache();
}

/* ── Controller Initialization ────────────────────────────────────────────── */

int nvme_init(void) {
    memset(&nvme_ctrl, 0, sizeof(nvme_controller_t));

    /* 1. Allocate 64 KiB contiguous bounce buffer */
    if (!nvme_dma_bounce) {
        nvme_dma_bounce = (uint8_t *)pmm_alloc_pages(16);
        if (!nvme_dma_bounce) return -1;
    }

    /* 2. Locate NVMe controller via PCI */
    pci_device_t *pci = pci_find_storage(PCI_SUBCLASS_STORAGE_NVME, PCI_PROG_IF_NVME);
    if (!pci) {
        return 0; /* No NVMe controller present on system */
    }

    nvme_ctrl.pci_dev = pci;

    /* 3. Enable Bus Mastering and Memory Space */
    pci_enable_bus_master(pci);
    pci_enable_mem_space(pci);

    /* 4. Map BAR0 MMIO space (at least 4 pages = 16 KiB) */
    uint64_t bar0_phys = pci->bar[0] & ~0xFULL;
    if (!bar0_phys) return -1;

    for (uint64_t p = 0; p < 4; p++) {
        uint64_t pa = bar0_phys + p * 4096;
        paging_map_page(pa, pa, PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE);
    }

    nvme_ctrl.mmio = (volatile uint8_t *)bar0_phys;

    /* 5. Read Capabilities */
    uint64_t cap = nvme_read64(NVME_REG_CAP);
    nvme_ctrl.doorbell_stride = (uint32_t)((cap >> 32) & 0x0F);

    /* 6. Reset Controller: Clear CC.EN if active */
    uint32_t csts = nvme_read32(NVME_REG_CSTS);
    if (csts & 1) {
        uint32_t cc = nvme_read32(NVME_REG_CC);
        nvme_write32(NVME_REG_CC, cc & ~1U);
        for (int t = 0; t < 50000; t++) {
            if (!(nvme_read32(NVME_REG_CSTS) & 1)) break;
        }
    }

    /* 7. Allocate Physical Pages for Admin Submission & Completion Queues */
    void *asq_page = pmm_alloc_page();
    void *acq_page = pmm_alloc_page();
    if (!asq_page || !acq_page) return -1;

    memset(asq_page, 0, 4096);
    memset(acq_page, 0, 4096);

    nvme_ctrl.admin_sq = (nvme_cmd_t *)asq_page;
    nvme_ctrl.admin_cq = (nvme_cqe_t *)acq_page;
    nvme_ctrl.admin_sq_tail = 0;
    nvme_ctrl.admin_cq_head = 0;
    nvme_ctrl.admin_cq_phase = 1;
    nvme_ctrl.admin_cid = 0;

    /* Configure AQA, ASQ, ACQ */
    uint32_t aqa = ((NVME_ADMIN_QUEUE_ENTRIES - 1) << 16) | (NVME_ADMIN_QUEUE_ENTRIES - 1);
    nvme_write32(NVME_REG_AQA, aqa);
    nvme_write64(NVME_REG_ASQ, (uint64_t)asq_page);
    nvme_write64(NVME_REG_ACQ, (uint64_t)acq_page);

    /* 8. Enable Controller: Set CC.EN = 1, IOSQES = 6 (64B), IOCQES = 4 (16B) */
    uint32_t cc = (6 << 16) | (4 << 20) | 1U;
    nvme_write32(NVME_REG_CC, cc);

    /* Wait for CSTS.RDY == 1 */
    for (int t = 0; t < 50000; t++) {
        if (nvme_read32(NVME_REG_CSTS) & 1) break;
    }

    if (!(nvme_read32(NVME_REG_CSTS) & 1)) {
        kwarn("NVMe: controller failed to become ready");
        return -1;
    }

    /* 9. Identify Controller (Admin Opcode 0x06, CNS = 1) */
    uint8_t *ident_buf = nvme_dma_bounce;
    memset(ident_buf, 0, 4096);

    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(nvme_cmd_t));
    cmd.opcode = NVME_ADMIN_OP_IDENTIFY;
    cmd.nsid = 0;
    cmd.prp1 = (uint64_t)ident_buf;
    cmd.cdw10 = 1; /* CNS = 1: Identify Controller */

    if (nvme_submit_admin_cmd(&cmd, NULL) != 0) {
        kwarn("NVMe: identify controller failed");
        return -1;
    }

    /* Extract Serial (bytes 4..23) and Model (bytes 24..63) */
    for (int i = 0; i < 20; i++) {
        nvme_ctrl.serial[i] = (char)ident_buf[4 + i];
    }
    nvme_ctrl.serial[20] = '\0';
    for (int i = 19; i >= 0 && nvme_ctrl.serial[i] == ' '; i--) {
        nvme_ctrl.serial[i] = '\0';
    }

    for (int i = 0; i < 40; i++) {
        nvme_ctrl.model[i] = (char)ident_buf[24 + i];
    }
    nvme_ctrl.model[40] = '\0';
    for (int i = 39; i >= 0 && nvme_ctrl.model[i] == ' '; i--) {
        nvme_ctrl.model[i] = '\0';
    }

    /* 10. Identify Namespace 1 (Admin Opcode 0x06, CNS = 0, NSID = 1) */
    memset(ident_buf, 0, 4096);
    memset(&cmd, 0, sizeof(nvme_cmd_t));
    cmd.opcode = NVME_ADMIN_OP_IDENTIFY;
    cmd.nsid = 1;
    cmd.prp1 = (uint64_t)ident_buf;
    cmd.cdw10 = 0; /* CNS = 0: Identify Namespace */

    if (nvme_submit_admin_cmd(&cmd, NULL) != 0) {
        kwarn("NVMe: identify namespace failed");
        return -1;
    }

    /* NSZE: Namespace Size in logical blocks (bytes 0..7) */
    memcpy(&nvme_ctrl.capacity_sectors, &ident_buf[0], sizeof(uint64_t));

    /* Formatted LBA size */
    uint8_t flbas = ident_buf[26] & 0x0F;
    uint8_t lbads = ident_buf[128 + flbas * 4 + 2];
    nvme_ctrl.sector_size = (lbads > 0) ? (1U << lbads) : NVME_DEFAULT_SECTOR_SIZE;

    /* 11. Create I/O Completion Queue (Admin Opcode 0x05) */
    void *io_cq_page = pmm_alloc_page();
    void *io_sq_page = pmm_alloc_page();
    if (!io_cq_page || !io_sq_page) return -1;

    memset(io_cq_page, 0, 4096);
    memset(io_sq_page, 0, 4096);

    nvme_ctrl.io_cq = (nvme_cqe_t *)io_cq_page;
    nvme_ctrl.io_sq = (nvme_cmd_t *)io_sq_page;
    nvme_ctrl.io_sq_tail = 0;
    nvme_ctrl.io_cq_head = 0;
    nvme_ctrl.io_cq_phase = 1;
    nvme_ctrl.io_cid = 0;

    memset(&cmd, 0, sizeof(nvme_cmd_t));
    cmd.opcode = NVME_ADMIN_OP_CREATE_IO_CQ;
    cmd.prp1 = (uint64_t)io_cq_page;
    cmd.cdw10 = ((NVME_IO_QUEUE_ENTRIES - 1) << 16) | 1U; /* QID = 1, size = 64 */
    cmd.cdw11 = 0x01; /* Physically Contiguous, Interrupts Disabled */

    if (nvme_submit_admin_cmd(&cmd, NULL) != 0) {
        kwarn("NVMe: create I/O completion queue failed");
        return -1;
    }

    /* 12. Create I/O Submission Queue (Admin Opcode 0x01) */
    memset(&cmd, 0, sizeof(nvme_cmd_t));
    cmd.opcode = NVME_ADMIN_OP_CREATE_IO_SQ;
    cmd.prp1 = (uint64_t)io_sq_page;
    cmd.cdw10 = ((NVME_IO_QUEUE_ENTRIES - 1) << 16) | 1U; /* QID = 1, size = 64 */
    cmd.cdw11 = (1U << 16) | 0x01; /* CQID = 1, Physically Contiguous */

    if (nvme_submit_admin_cmd(&cmd, NULL) != 0) {
        kwarn("NVMe: create I/O submission queue failed");
        return -1;
    }

    nvme_ctrl.initialized = true;

    /* 13. Register Block Device /dev/nvme0 */
    memset(&nvme_ctrl.block_dev, 0, sizeof(block_device_t));
    strncpy(nvme_ctrl.block_dev.name, "nvme0", BLOCK_DEV_NAME_MAX - 1);
    strncpy(nvme_ctrl.block_dev.type_str, "NVME", sizeof(nvme_ctrl.block_dev.type_str) - 1);
    strncpy(nvme_ctrl.block_dev.model, nvme_ctrl.model[0] ? nvme_ctrl.model : "NVMe SSD", sizeof(nvme_ctrl.block_dev.model) - 1);
    nvme_ctrl.block_dev.sector_size = nvme_ctrl.sector_size;
    nvme_ctrl.block_dev.capacity_sectors = nvme_ctrl.capacity_sectors;
    nvme_ctrl.block_dev.sector_count = nvme_ctrl.capacity_sectors;
    nvme_ctrl.block_dev.read_only = false;
    nvme_ctrl.block_dev.present = true;
    nvme_ctrl.block_dev.read = nvme_bdev_read;
    nvme_ctrl.block_dev.write = nvme_bdev_write;
    nvme_ctrl.block_dev.flush = nvme_bdev_flush;
    nvme_ctrl.block_dev.private_data = &nvme_ctrl;

    block_device_register(&nvme_ctrl.block_dev);

    serial_write("[NVMe] Registered /dev/nvme0 (Model: ");
    serial_write(nvme_ctrl.block_dev.model);
    serial_write(", Sectors: ");
    serial_write_dec(nvme_ctrl.capacity_sectors);
    serial_write(")\n");

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ]  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("NVMe (");
    vga_print(nvme_ctrl.block_dev.model);
    vga_println(" online)");

    return 1;
}

bool nvme_is_present(void) {
    return nvme_ctrl.initialized;
}
