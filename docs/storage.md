# Nyota OS Storage & Modern Hardware Architecture

**Target Release**: Nyota OS v1.2.0  
**Architecture**: x86_64 Long Mode (SMP-aware)  
**Status**: Production / Tested  

---

## 1. Unified Block-Device Architecture

Nyota OS implements an extensible, thread-safe block-device abstraction in [include/storage/block.h](file:///c:/Users/HP/Projects/Nyota%20OS/include/storage/block.h) and [kernel/storage/block.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/storage/block.c). Every hardware disk controller, logical drive, and discovered partition registers a `struct block_device`:

```c
struct block_device {
    char name[32];
    uint64_t capacity_sectors;
    uint32_t sector_size;
    bool read_only;
    bool present;
    bool is_partition;
    uint64_t part_start_lba;
    struct block_device *parent;
    void *priv;

    int (*read)(struct block_device *dev, uint64_t lba, uint32_t count, void *buf);
    int (*write)(struct block_device *dev, uint64_t lba, uint32_t count, const void *buf);
    int (*flush)(struct block_device *dev);
};
```

### Key Guarantees
1. **Multi-Driver Coexistence**: ATA PIO, AHCI SATA, and NVMe controllers register into a centralized device table guarded by `spinlock_t`.
2. **Boundary Validation**: Sector counts and LBA ranges are validated before invoking hardware drivers (`lba + count <= capacity_sectors`). Out-of-bounds requests return `NYOTA_EINVAL` immediately without touching device registers.
3. **Write Protection**: Any write operation to a read-only device (`read_only == true`) is rejected with `NYOTA_EROFS`.
4. **Partition Forwarding**: Discovered partitions are first-class block devices that translate logical partition LBAs into absolute parent disk LBAs (`parent_lba = dev->part_start_lba + lba`) and forward operations to the parent driver.
5. **Concurrency Safety**: Spinlocks guard device registration, lookup, and reference counting; disk I/O callbacks are invoked outside critical sections to prevent priority inversions and deadlock under SMP.

---

## 2. Storage Controllers & Drivers

### 2.1 ATA PIO (`ata0`, `ata1`)
* **Source**: [include/storage/ata.h](file:///c:/Users/HP/Projects/Nyota%20OS/include/storage/ata.h), [kernel/storage/ata.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/storage/ata.c)
* **Ports**: Primary bus (`0x1F0`–`0x1F7`, control `0x3F6`), Secondary bus (`0x170`–`0x177`, control `0x376`).
* **Protocol**: LBA28 with automatic LBA48 fallback.
* **IDENTIFY**: Reads drive model, geometry, and sector count.
* **Capabilities**: Reads, writes, hardware cache flush (`0xE7`), and error status decoding (`ERR`, `DF`, `DRQ`).

### 2.2 AHCI SATA (`sda`, `sdb`, ...)
* **Source**: [include/storage/ahci.h](file:///c:/Users/HP/Projects/Nyota%20OS/include/storage/ahci.h), [kernel/storage/ahci.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/storage/ahci.c)
* **PCI Discovery**: Matches Class `0x01` (Mass Storage), Subclass `0x06` (SATA), Prog-IF `0x01` (AHCI).
* **ABAR Mapping**: Automatically verifies and programs BAR5 (ABAR) into the 32-bit PCI MMIO aperture if unassigned by firmware. Maps HBA registers as uncached supervisor pages.
* **Initialization**:
  - Sets `GHC.AE` (AHCI Enable) and performs HBA reset (`GHC.HR`).
  - Detects implemented ports (`PI` bitmask).
  - Configures Spin-Up (`CMD.SUD`) and Power-On Device (`CMD.POD`).
  - Allocates 1 KiB Command List, 256 B Received FIS, and Command Tables with 1 PRDT entry per transfer.
* **DMA Protocol**: FIS Type `0x27` (Register H2D) issuing ATA commands:
  - `ATA_CMD_IDENTIFY` (`0xEC`)
  - `ATA_CMD_READ_DMA_EX` (`0x25`)
  - `ATA_CMD_WRITE_DMA_EX` (`0x35`)
  - `ATA_CMD_FLUSH_CACHE_EXT` (`0xEA`)

### 2.3 NVMe Controller (`nvme0`, `nvme1`, ...)
* **Source**: [include/storage/nvme.h](file:///c:/Users/HP/Projects/Nyota%20OS/include/storage/nvme.h), [kernel/storage/nvme.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/storage/nvme.c)
* **PCI Discovery**: Matches Class `0x01` (Mass Storage), Subclass `0x08` (Non-Volatile Memory), Prog-IF `0x02` (NVM Express).
* **MMIO Registers**: Maps BAR0 into virtual memory. Enforces 32-bit/64-bit atomic access via explicit inline assembly (`movl`/`movq`) to prevent partial MMIO read optimizations.
* **Initialization Sequence**:
  - Clears `CC.EN` and polls `CSTS.RDY == 0` for controller reset.
  - Configures Admin Submission Queue (ASQ) and Completion Queue (ACQ).
  - Sets page size (4 KiB) and round-robin arbitration.
  - Sets `CC.EN = 1` and waits for `CSTS.RDY == 1`.
  - Submits Identify Controller (CNS=1) to retrieve serial number, model, and SQ/CQ capabilities.
  - Submits Identify Namespace (CNS=0, NSID=1) to retrieve LBA capacity and formatted sector size.
  - Allocates and creates I/O Submission Queue (SQ1) and Completion Queue (CQ1).
* **I/O Commands**:
  - `NVME_NVM_READ` (`0x02`)
  - `NVME_NVM_WRITE` (`0x01`)
  - `NVME_NVM_FLUSH` (`0x00`)
* **Doorbell Ringing**: Calculated using controller doorbell stride (`DSTRD`):
  `offset = 0x1000 + (2 * qid + direction) * (4 << dstrd)`

---

## 3. DMA-Safe Memory Management

DMA controllers (AHCI and NVMe) require device-visible, physically contiguous buffers that adhere to hardware alignment rules:

1. **Physical Contiguity**: PMM-allocated 4 KiB physical frames are used for all Command Lists, FIS receive buffers, Command Tables, and NVMe Submission/Completion Queues.
2. **Alignment**:
   - AHCI Command List: 1024-byte aligned.
   - AHCI Received FIS: 256-byte aligned.
   - AHCI Command Table: 128-byte aligned.
   - NVMe Submission/Completion Queues: 4096-byte aligned.
3. **Physical Translation**: Virtual buffers are translated to physical frames via `paging_get_physical()`. User-space buffers are transferred via kernel bounce buffers allocated from PMM frames to prevent device DMA corruption of non-contiguous user memory.

---

## 4. Partition Discovery (MBR & GPT)

Implemented in [include/storage/partition.h](file:///c:/Users/HP/Projects/Nyota%20OS/include/storage/partition.h) and [kernel/storage/partition.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/storage/partition.c).

### 4.1 Master Boot Record (MBR)
* Validates boot signature `0xAA55` at offset 510 of sector 0.
* Inspects 4 primary partition records at offset 446 (16 bytes each).
* Rejects empty partitions (`type == 0x00`) or invalid ranges (`sectors == 0`).
* Registers sub-device (e.g. `/dev/sda1`) with start LBA and sector count.

### 4.2 GUID Partition Table (GPT)
* Verifies Protective MBR at LBA 0 (Partition type `0xEE`).
* Reads GPT Header at LBA 1 and validates signature `"EFI PART"` (`0x5452415020494645ULL`).
* Validates Header CRC32 (IEEE 802.3 Ethernet polynomial `0xEDB88320`) with CRC field zeroed during computation.
* Reads Partition Entry Array (LBA 2..33) and verifies Partition Array CRC32 against header checksum.
* Validates starting and ending LBAs (`ending_lba >= starting_lba` and within disk capacity).
* Rejects empty partitions (null Type GUID).
* Registers sub-devices with standard naming (e.g., `/dev/nvme0p1`).

---

## 5. Storage Block Cache

Implemented in [include/storage/cache.h](file:///c:/Users/HP/Projects/Nyota%20OS/include/storage/cache.h) and [kernel/storage/cache.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/storage/cache.c).

* **Architecture**: Bounded LRU (Least Recently Used) cache with 64 lines (64 KiB memory budget).
* **Dirty Block Tracking**: Write operations update cache buffers and flag the line `dirty = true`.
* **Writeback**: Eviction of dirty cache lines flushes data back to the underlying block device before assigning the line to new sectors.
* **APIs**:
  - `block_cache_read(bdev, lba, count, buf)`
  - `block_cache_write(bdev, lba, count, buf)`
  - `block_cache_flush(bdev)`
  - `block_cache_invalidate(bdev)`
* **VFS Integration**: System call `sync` (`SYS_SYNC`) and `vfs_sync()` trigger a full writeback and flush across all registered devices.

---

## 6. Filesystem Integration & Syscalls

* **NyotaFS**: Superblock, inode table, block allocation bitmap, and directory walks use the block cache for accelerated metadata and data caching.
* **New Syscalls**:
  - `SYS_PCI_INFO` (71): Returns list of detected PCI devices, vendor/device IDs, class/subclass, IRQ, and BAR configurations.
  - `SYS_BLK_INFO` (72): Returns list of detected block devices and partitions with capacities and sector sizes.
  - `SYS_BLK_READ` (73): Reads sectors from a block device (requires root/CAP_SYS_ADMIN).
  - `SYS_BLK_WRITE` (74): Writes sectors to a block device (requires root/CAP_SYS_ADMIN).
  - `SYS_BLK_FLUSH` (75): Flushes hardware write caches.
  - `SYS_STATVFS` (76): Returns real filesystem capacity, free blocks, block size, and inode usage.

---

## 7. Storage Diagnostics & Utilities

| Utility | Description | Example Output |
|---------|-------------|----------------|
| `/bin/lspci` | Enumerate PCI bus, device IDs, classes, IRQs, and BARs | `00:04.0 SATA controller (AHCI) [0x8086:0x2922] IRQ 11` |
| `/bin/lsblk` | List all discovered block devices, types, sizes, statuses | `/dev/sda AHCI 16 MiB ONLINE` |
| `/bin/diskinfo` | Comprehensive disk architecture and sector layout inspection | `Capacity: 16 MiB (32768 sectors), Sector Size: 512` |
| `/bin/df` | Report filesystem disk space usage via `statvfs` | `nyotafs 16M 2M 13M 13% /` |
| `/bin/disktest` | Comprehensive, non-destructive hardware verification tool | Reads, boundary check, CRC32 cache consistency, flush |

---

## 8. Verification & Test Suite

The automated storage test harness [tools/test_storage.py](file:///c:/Users/HP/Projects/Nyota%20OS/tools/test_storage.py) runs on QEMU:

```bash
python tools/test_storage.py
```

### Verified Test Results
```text
============================================================
           NYOTA OS STORAGE TEST REPORT
============================================================
  [ PASS ] lspci_enumeration
  [ PASS ] lsblk_listing
  [ PASS ] diskinfo_inspection
  [ PASS ] df_statvfs
  [ PASS ] disktest_safe_suite
  [ PASS ] disktest_sda
  [ PASS ] disktest_nvme
  [ PASS ] root_disk_protection
============================================================
ALL STORAGE & HARDWARE TESTS PASSED SUCCESSFULLY!
```
