/* =============================================================================
 * Nyota OS — lsblk User Utility (Phase 12)
 * Lists all detected block devices and partitions via sys_blk_info.
 * =========================================================================== */

#include "libnyota.h"

static void format_size(uint64_t sectors, uint32_t sec_size, char *out, size_t out_len) {
    if (sec_size == 0) sec_size = 512;
    uint64_t bytes = sectors * sec_size;

    if (bytes >= (1024ULL * 1024 * 1024)) {
        uint64_t gib = bytes / (1024ULL * 1024 * 1024);
        snprintf(out, out_len, "%u GiB", (unsigned int)gib);
    } else if (bytes >= (1024ULL * 1024)) {
        uint64_t mib = bytes / (1024ULL * 1024);
        snprintf(out, out_len, "%u MiB", (unsigned int)mib);
    } else if (bytes >= 1024) {
        uint64_t kib = bytes / 1024;
        snprintf(out, out_len, "%u KiB", (unsigned int)kib);
    } else {
        snprintf(out, out_len, "%u B", (unsigned int)bytes);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    blk_info_list_t blist;
    if (blk_info(&blist) != 0) {
        printf("lsblk: error querying block devices\n");
        return 1;
    }

    printf("Nyota Storage Devices\n");
    printf("---------------------\n");
    printf("%-12s %-10s %-10s %s\n", "DEVICE", "TYPE", "SIZE", "STATUS");

    if (blist.count == 0) {
        printf("No storage devices detected.\n");
        return 0;
    }

    for (uint32_t i = 0; i < blist.count; i++) {
        blk_info_entry_t *dev = &blist.devices[i];

        char dev_path[40];
        snprintf(dev_path, sizeof(dev_path), "/dev/%s", dev->name);

        char sz_str[24];
        format_size(dev->capacity_sectors, dev->sector_size, sz_str, sizeof(sz_str));

        const char *type_name = dev->type_str;
        if (dev->is_partition) {
            type_name = "PARTITION";
        }

        const char *status = "ONLINE";
        if (dev->read_only) {
            status = "READONLY";
        } else if (dev->is_partition) {
            status = "DETECTED";
        }

        printf("%-12s %-10s %-10s %s\n", dev_path, type_name, sz_str, status);
    }

    return 0;
}
