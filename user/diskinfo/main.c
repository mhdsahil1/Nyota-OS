/* =============================================================================
 * Nyota OS — diskinfo User Utility (Phase 12)
 * Detailed disk and controller hardware inspection via sys_blk_info.
 * =========================================================================== */

#include "libnyota.h"

static void print_disk_details(blk_info_entry_t *dev) {
    uint32_t sec_size = dev->sector_size ? dev->sector_size : 512;
    uint64_t total_bytes = dev->capacity_sectors * sec_size;
    uint64_t mib = total_bytes / (1024ULL * 1024);
    uint64_t gib = total_bytes / (1024ULL * 1024 * 1024);

    printf("Device:              /dev/%s\n", dev->name);
    printf("Type:                %s\n", dev->type_str);
    printf("Model / Description: %s\n", dev->model[0] ? dev->model : "Generic Block Device");
    if (gib > 0) {
        printf("Capacity:            %u GiB (%u MiB, %lu sectors)\n",
               (unsigned int)gib, (unsigned int)mib, (unsigned long)dev->capacity_sectors);
    } else {
        printf("Capacity:            %u MiB (%lu sectors)\n",
               (unsigned int)mib, (unsigned long)dev->capacity_sectors);
    }
    printf("Logical Sector Size: %u bytes\n", sec_size);
    printf("Physical Sector:     %u bytes\n", sec_size);
    printf("Read-Only:           %s\n", dev->read_only ? "YES (write protected)" : "NO (read-write)");
    printf("Driver:              %s\n", dev->type_str);

    if (dev->is_partition) {
        printf("Partition Of:        /dev/%s\n", dev->parent_name);
        printf("Starting LBA:        %lu\n", (unsigned long)dev->partition_offset);
        printf("Status:              VALID / DETECTED\n");
    } else {
        printf("Status:              ONLINE / READY\n");
    }
    printf("\n");
}

int main(int argc, char **argv) {
    const char *target = NULL;
    if (argc > 1) {
        target = argv[1];
        if (strncmp(target, "/dev/", 5) == 0) {
            target += 5;
        }
    }

    blk_info_list_t blist;
    if (blk_info(&blist) != 0) {
        printf("diskinfo: error querying block devices\n");
        return 1;
    }

    if (blist.count == 0) {
        printf("diskinfo: no block devices found.\n");
        return 0;
    }

    if (target) {
        bool found = false;
        for (uint32_t i = 0; i < blist.count; i++) {
            if (strcmp(blist.devices[i].name, target) == 0) {
                print_disk_details(&blist.devices[i]);
                found = true;
                break;
            }
        }
        if (!found) {
            printf("diskinfo: device '/dev/%s' not found.\n", target);
            return 1;
        }
        return 0;
    }

    printf("Nyota Storage Architecture & Disk Diagnostics\n");
    printf("==============================================\n\n");

    for (uint32_t i = 0; i < blist.count; i++) {
        print_disk_details(&blist.devices[i]);
    }

    return 0;
}
