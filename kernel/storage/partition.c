/* =============================================================================
 * Nyota OS — Partition Discovery Subsystem Implementation (Phase 12)
 * Robust MBR and GPT partition discovery, signature & CRC32 validation,
 * boundary validation, and partition device registration.
 * =========================================================================== */

#include "storage/partition.h"
#include "storage/block.h"
#include "memory.h"
#include "vga.h"
#include "serial.h"
#include "kernel.h"
#include "spinlock.h"

static block_device_t partition_pool[MAX_PARTITIONS_TOTAL];
static size_t partition_pool_count = 0;
static spinlock_t part_lock = SPINLOCK_INIT;

/* ── IEEE 802.3 CRC32 Implementation for GPT Validation ───────────────────── */

uint32_t partition_crc32(const void *data, size_t length) {
    const uint8_t *buf = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;

    for (size_t i = 0; i < length; i++) {
        uint8_t byte = buf[i];
        crc ^= byte;
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

void partition_init(void) {
    uint64_t rflags = spinlock_acquire_irqsave(&part_lock);
    memset(partition_pool, 0, sizeof(partition_pool));
    partition_pool_count = 0;
    spinlock_release_irqrestore(&part_lock, rflags);
    partition_scan_all();
}

/* Format partition device name: e.g. "sda" -> "sda1", "nvme0" -> "nvme0p1" */
static void format_part_name(char *out, size_t out_len, const char *parent_name, int part_num) {
    size_t plen = strlen(parent_name);
    char last_char = (plen > 0) ? parent_name[plen - 1] : '\0';

    if (last_char >= '0' && last_char <= '9') {
        /* Ends in digit: e.g. ata0p1, nvme0p1 */
        int w = 0;
        for (size_t i = 0; i < plen && w < (int)out_len - 10; i++) {
            out[w++] = parent_name[i];
        }
        out[w++] = 'p';
        if (part_num >= 10) out[w++] = (char)('0' + (part_num / 10));
        out[w++] = (char)('0' + (part_num % 10));
        out[w] = '\0';
    } else {
        /* Ends in letter: e.g. sda1, sdb2 */
        int w = 0;
        for (size_t i = 0; i < plen && w < (int)out_len - 10; i++) {
            out[w++] = parent_name[i];
        }
        if (part_num >= 10) out[w++] = (char)('0' + (part_num / 10));
        out[w++] = (char)('0' + (part_num % 10));
        out[w] = '\0';
    }
}

/* ── GPT Partition Discovery ──────────────────────────────────────────────── */

static int scan_gpt(block_device_t *disk) {
    if (!disk || disk->capacity_sectors < 34) return 0;

    /* Read LBA 1 (Primary GPT Header) */
    uint8_t hdr_buf[512];
    if (block_device_read(disk, 1, 1, hdr_buf) != 0) {
        return 0;
    }

    gpt_header_t hdr;
    memcpy(&hdr, hdr_buf, sizeof(gpt_header_t));

    /* 1. Validate signature "EFI PART" */
    if (memcmp(hdr.signature, GPT_SIGNATURE, 8) != 0) {
        return 0;
    }

    /* 2. Validate header size */
    if (hdr.header_size < 92 || hdr.header_size > 512) {
        kwarn("GPT: invalid header size");
        return 0;
    }

    /* 3. Validate header CRC32 */
    uint32_t orig_crc = hdr.header_crc32;
    hdr.header_crc32 = 0;
    uint32_t calc_crc = partition_crc32(&hdr, hdr.header_size);
    if (calc_crc != orig_crc) {
        kwarn("GPT: header CRC32 mismatch, corrupt header");
        return 0;
    }

    /* 4. Validate partition entries dimensions */
    if (hdr.sizeof_partition_entry < 128 || hdr.num_partition_entries == 0 ||
        hdr.num_partition_entries > 128) {
        kwarn("GPT: invalid partition array dimensions");
        return 0;
    }

    /* 5. Read partition entries array (typically 128 entries * 128 bytes = 16 KiB = 32 sectors) */
    uint32_t total_bytes = hdr.num_partition_entries * hdr.sizeof_partition_entry;
    uint32_t sectors_to_read = (total_bytes + disk->sector_size - 1) / disk->sector_size;

    if (sectors_to_read > 32) sectors_to_read = 32;

    uint8_t entries_buf[16384];
    if (block_device_read(disk, hdr.partition_entries_lba, sectors_to_read, entries_buf) != 0) {
        kwarn("GPT: failed to read partition entry array");
        return 0;
    }

    /* 6. Validate partition array CRC32 */
    uint32_t array_crc = partition_crc32(entries_buf, total_bytes);
    if (array_crc != hdr.partition_array_crc32) {
        kwarn("GPT: partition array CRC32 mismatch, corrupt partition table");
        return 0;
    }

    int found_count = 0;

    /* 7. Parse entries */
    for (uint32_t i = 0; i < hdr.num_partition_entries; i++) {
        gpt_entry_t *entry = (gpt_entry_t *)(entries_buf + i * hdr.sizeof_partition_entry);

        /* Check if type GUID is all zeroes (unused slot) */
        bool is_empty = true;
        for (int b = 0; b < 16; b++) {
            if (entry->type_guid[b] != 0) {
                is_empty = false;
                break;
            }
        }
        if (is_empty) continue;

        /* Validate boundary constraints */
        if (entry->starting_lba > entry->ending_lba ||
            entry->ending_lba >= disk->capacity_sectors ||
            entry->starting_lba < hdr.first_usable_lba ||
            entry->ending_lba > hdr.last_usable_lba) {
            kwarn("GPT: out-of-bounds partition entry rejected");
            continue;
        }

        uint64_t part_sectors = entry->ending_lba - entry->starting_lba + 1;

        /* Allocate partition block device */
        uint64_t rflags = spinlock_acquire_irqsave(&part_lock);
        if (partition_pool_count >= MAX_PARTITIONS_TOTAL) {
            spinlock_release_irqrestore(&part_lock, rflags);
            kwarn("partition_scan: partition pool exhausted");
            break;
        }

        block_device_t *pdev = &partition_pool[partition_pool_count++];
        memset(pdev, 0, sizeof(block_device_t));

        format_part_name(pdev->name, sizeof(pdev->name), disk->name, (int)(i + 1));
        pdev->parent = disk;
        pdev->partition_offset_lba = entry->starting_lba;
        pdev->capacity_sectors = part_sectors;
        pdev->sector_count = part_sectors;
        pdev->sector_size = disk->sector_size;
        pdev->read_only = disk->read_only;
        pdev->present = true;
        pdev->partition_num = (uint8_t)(i + 1);
        strncpy(pdev->type_str, "PARTITION", sizeof(pdev->type_str) - 1);

        /* Extract UTF-16LE label into ASCII model */
        for (int c = 0; c < 35 && entry->name[c] != 0; c++) {
            pdev->model[c] = (char)(entry->name[c] & 0x7F);
            pdev->model[c + 1] = '\0';
        }
        if (!pdev->model[0]) {
            strncpy(pdev->model, "GPT Partition", sizeof(pdev->model) - 1);
        }

        spinlock_release_irqrestore(&part_lock, rflags);

        block_device_register(pdev);
        found_count++;

        serial_write("[PART] GPT Partition: ");
        serial_write(pdev->name);
        serial_write(" start=");
        serial_write_dec(pdev->partition_offset_lba);
        serial_write(" sectors=");
        serial_write_dec(pdev->capacity_sectors);
        serial_write("\n");
    }

    return found_count;
}

/* ── MBR Partition Discovery ──────────────────────────────────────────────── */

static int scan_mbr(block_device_t *disk, const uint8_t *sec0) {
    const mbr_sector_t *mbr = (const mbr_sector_t *)sec0;

    /* Validate boot signature (0x55, 0xAA at 510..511) */
    if (mbr->signature != MBR_SIGNATURE) {
        return 0;
    }

    /* Check for GPT Protective MBR (type == 0xEE) */
    for (int i = 0; i < MBR_PARTITION_COUNT; i++) {
        if (mbr->entries[i].type == 0xEE) {
            int gpt_found = scan_gpt(disk);
            if (gpt_found > 0) {
                return gpt_found;
            }
        }
    }

    int found_count = 0;

    /* Validate and register primary MBR partitions */
    for (int i = 0; i < MBR_PARTITION_COUNT; i++) {
        const mbr_entry_t *entry = &mbr->entries[i];

        if (entry->type == 0x00 || entry->sector_count == 0) {
            continue;
        }

        uint64_t start_lba = entry->lba_start;
        uint64_t count = entry->sector_count;

        /* Validate boundary constraints */
        if (start_lba == 0 || count == 0 ||
            start_lba + count < start_lba ||
            start_lba + count > disk->capacity_sectors) {
            kwarn("MBR: out-of-bounds partition entry rejected");
            continue;
        }

        /* Check for overlaps with already discovered partitions */
        bool overlap = false;
        for (int j = 0; j < i; j++) {
            const mbr_entry_t *prev = &mbr->entries[j];
            if (prev->type != 0 && prev->sector_count > 0) {
                uint64_t p_start = prev->lba_start;
                uint64_t p_end = p_start + prev->sector_count;
                uint64_t c_end = start_lba + count;
                if ((start_lba >= p_start && start_lba < p_end) ||
                    (c_end > p_start && c_end <= p_end)) {
                    overlap = true;
                    break;
                }
            }
        }
        if (overlap) {
            kwarn("MBR: overlapping partition rejected");
            continue;
        }

        /* Allocate partition block device */
        uint64_t rflags = spinlock_acquire_irqsave(&part_lock);
        if (partition_pool_count >= MAX_PARTITIONS_TOTAL) {
            spinlock_release_irqrestore(&part_lock, rflags);
            kwarn("partition_scan: partition pool exhausted");
            break;
        }

        block_device_t *pdev = &partition_pool[partition_pool_count++];
        memset(pdev, 0, sizeof(block_device_t));

        format_part_name(pdev->name, sizeof(pdev->name), disk->name, i + 1);
        pdev->parent = disk;
        pdev->partition_offset_lba = start_lba;
        pdev->capacity_sectors = count;
        pdev->sector_count = count;
        pdev->sector_size = disk->sector_size;
        pdev->read_only = disk->read_only;
        pdev->present = true;
        pdev->partition_num = (uint8_t)(i + 1);
        pdev->partition_type = entry->type;
        strncpy(pdev->type_str, "PARTITION", sizeof(pdev->type_str) - 1);

        /* Set model / type label */
        if (entry->type == 0x83) {
            strncpy(pdev->model, "Linux Native / Data", sizeof(pdev->model) - 1);
        } else if (entry->type == 0x07) {
            strncpy(pdev->model, "NTFS / exFAT / HPFS", sizeof(pdev->model) - 1);
        } else if (entry->type == 0x0B || entry->type == 0x0C) {
            strncpy(pdev->model, "FAT32", sizeof(pdev->model) - 1);
        } else if (entry->type == 0x4E) {
            strncpy(pdev->model, "NyotaFS", sizeof(pdev->model) - 1);
        } else {
            strncpy(pdev->model, "MBR Partition", sizeof(pdev->model) - 1);
        }

        spinlock_release_irqrestore(&part_lock, rflags);

        block_device_register(pdev);
        found_count++;

        serial_write("[PART] MBR Partition: ");
        serial_write(pdev->name);
        serial_write(" start=");
        serial_write_dec(pdev->partition_offset_lba);
        serial_write(" sectors=");
        serial_write_dec(pdev->capacity_sectors);
        serial_write("\n");
    }

    return found_count;
}

int partition_scan(block_device_t *disk) {
    if (!disk || !disk->present || disk->parent != NULL || disk->capacity_sectors < 2) {
        return 0;
    }

    uint8_t sec0[512];
    if (block_device_read(disk, 0, 1, sec0) != 0) {
        return 0;
    }

    return scan_mbr(disk, sec0);
}

void partition_scan_all(void) {
    size_t count = block_device_count();
    for (size_t i = 0; i < count; i++) {
        block_device_t *b = block_device_get((uint32_t)i);
        if (b && b->parent == NULL && b->present) {
            partition_scan(b);
        }
    }
}

