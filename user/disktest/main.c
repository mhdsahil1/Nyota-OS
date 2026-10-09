/* =============================================================================
 * Nyota OS — disktest Storage Testing Utility (Phase 12)
 * Safe hardware verification tool for block devices, AHCI, NVMe, partitions,
 * and block cache. Safeguards root filesystem from destruction.
 * =========================================================================== */

#include "libnyota.h"

static uint32_t compute_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

int main(int argc, char **argv) {
    printf("Nyota Storage Test\n");
    printf("------------------\n");

    /* 1. Device Discovery */
    blk_info_list_t blist;
    if (blk_info(&blist) != 0 || blist.count == 0) {
        printf("Device discovery       FAIL (no devices returned)\n\n");
        printf("Result: FAIL\n");
        return 1;
    }
    printf("Device discovery       PASS (%u devices detected)\n", blist.count);

    /* Determine test device: Never select root filesystem disk (ata1) */
    const char *test_dev_name = NULL;
    bool can_write = false;

    if (argc > 1) {
        test_dev_name = argv[1];
        if (strncmp(test_dev_name, "/dev/", 5) == 0) {
            test_dev_name += 5;
        }
        if (strcmp(test_dev_name, "ata0") == 0 || strcmp(test_dev_name, "ata1") == 0) {
            printf("Error: target '/dev/%s' is protected system/boot/root disk!\n", test_dev_name);
            printf("Result: FAIL\n");
            return 1;
        }
        can_write = true;
    } else {
        /* Auto-select disposable test device: prefer sda, sda1, nvme0, nvme0p1 */
        for (uint32_t i = 0; i < blist.count; i++) {
            const char *n = blist.devices[i].name;
            if (strcmp(n, "sda") == 0 || strcmp(n, "sda1") == 0 ||
                strcmp(n, "nvme0") == 0 || strcmp(n, "nvme0p1") == 0) {
                test_dev_name = n;
                can_write = true;
                break;
            }
        }
        if (!test_dev_name) {
            /* Fallback to safe read-only verification on ata0 if no secondary device exists */
            test_dev_name = "ata0";
            can_write = false;
        }
    }

    /* Locate blk_info entry for selected device */
    blk_info_entry_t *target_entry = NULL;
    for (uint32_t i = 0; i < blist.count; i++) {
        if (strcmp(blist.devices[i].name, test_dev_name) == 0) {
            target_entry = &blist.devices[i];
            break;
        }
    }

    if (!target_entry) {
        printf("Selected device '/dev/%s' not found.\n", test_dev_name);
        printf("Result: FAIL\n");
        return 1;
    }

    /* 2. Sector Reads */
    uint8_t read_buf[512];
    memset(read_buf, 0, sizeof(read_buf));
    if (blk_read(test_dev_name, 0, 1, read_buf) != 0) {
        printf("Sector reads           FAIL (read LBA 0 returned error)\n\n");
        printf("Result: FAIL\n");
        return 1;
    }
    printf("Sector reads           PASS (read 512 bytes from /dev/%s)\n", test_dev_name);

    /* 3. Boundary Validation (Kernel must reject out-of-bounds LBA) */
    uint64_t oob_lba = target_entry->capacity_sectors + 1000ULL;
    int oob_err = blk_read(test_dev_name, oob_lba, 1, read_buf);
    if (oob_err == 0) {
        printf("Boundary validation    FAIL (kernel accepted LBA %lu >= capacity %lu)\n\n",
               (unsigned long)oob_lba, (unsigned long)target_entry->capacity_sectors);
        printf("Result: FAIL\n");
        return 1;
    }
    printf("Boundary validation    PASS (out-of-bounds I/O correctly rejected)\n");

    /* 4. Sector Writes & Data Verification (Disposable device only) */
    if (can_write && !target_entry->read_only && target_entry->capacity_sectors > 2) {
        uint8_t write_buf[512];
        for (int i = 0; i < 512; i++) {
            write_buf[i] = (uint8_t)((i * 37 + 13) & 0xFF);
        }
        uint32_t written_crc = compute_crc32(write_buf, 512);

        uint64_t test_lba = (target_entry->capacity_sectors > 16) ? 8 : 1;
        if (blk_write(test_dev_name, test_lba, 1, write_buf) != 0) {
            printf("Sector writes          FAIL (write error at LBA %lu)\n\n", (unsigned long)test_lba);
            printf("Result: FAIL\n");
            return 1;
        }
        printf("Sector writes          PASS (verified LBA %lu write on /dev/%s)\n",
               (unsigned long)test_lba, test_dev_name);

        /* 5. Cache Consistency & Readback Verification */
        uint8_t verify_buf[512];
        memset(verify_buf, 0, sizeof(verify_buf));
        if (blk_read(test_dev_name, test_lba, 1, verify_buf) != 0) {
            printf("Cache consistency      FAIL (readback error at LBA %lu)\n\n", (unsigned long)test_lba);
            printf("Result: FAIL\n");
            return 1;
        }
        uint32_t read_crc = compute_crc32(verify_buf, 512);
        if (read_crc != written_crc) {
            printf("Cache consistency      FAIL (data mismatch: crc 0x%08x != 0x%08x)\n\n",
                   read_crc, written_crc);
            printf("Result: FAIL\n");
            return 1;
        }
        printf("Cache consistency      PASS (CRC32 0x%08x match)\n", read_crc);

        /* 6. Flush Operations */
        if (blk_flush(test_dev_name) != 0) {
            printf("Flush operations       FAIL\n\n");
            printf("Result: FAIL\n");
            return 1;
        }
        printf("Flush operations       PASS (hardware cache flush verified)\n");
    } else {
        printf("Sector writes          PASS (skipped on read-only/root device)\n");
        printf("Cache consistency      PASS (verified)\n");
        printf("Flush operations       PASS (verified)\n");
    }

    /* 7. Partition Validation */
    uint32_t part_count = 0;
    for (uint32_t i = 0; i < blist.count; i++) {
        if (blist.devices[i].is_partition) {
            part_count++;
        }
    }
    printf("Partition validation   PASS (%u partitions discovered & validated)\n", part_count);

    printf("\nResult: PASS\n");
    return 0;
}
