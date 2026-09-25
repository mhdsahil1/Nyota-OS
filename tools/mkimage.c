/* =============================================================================
 * Nyota OS — Disk Image Assembler (mkimage)
 * Combines boot.bin, stage2.bin, and kernel.bin into a bootable floppy/raw image.
 * Works uniformly across Windows, Linux, and macOS without external dependencies.
 * =========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SECTOR_SIZE       512
#define TOTAL_SECTORS     2880                /* 1.44 MB standard floppy / raw disk */
#define TOTAL_IMAGE_SIZE  (TOTAL_SECTORS * SECTOR_SIZE)

#define BOOT_SECTOR_OFFSET   0
#define STAGE2_SECTOR_OFFSET 1
#define STAGE2_SECTOR_COUNT  4
#define KERNEL_SECTOR_OFFSET 5

static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Could not open file '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0) {
        fclose(f);
        return NULL;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);

    *out_size = (size_t)sz;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <boot.bin> <stage2.bin> <kernel.bin> <output.img>\n", argv[0]);
        return 1;
    }

    const char *boot_path   = argv[1];
    const char *stage2_path = argv[2];
    const char *kernel_path = argv[3];
    const char *out_path    = argv[4];

    size_t boot_size = 0, stage2_size = 0, kernel_size = 0;
    uint8_t *boot_data   = read_file(boot_path, &boot_size);
    uint8_t *stage2_data = read_file(stage2_path, &stage2_size);
    uint8_t *kernel_data = read_file(kernel_path, &kernel_size);

    if (!boot_data || !stage2_data || !kernel_data) {
        fprintf(stderr, "Error: Failed to read input binary files.\n");
        return 1;
    }

    /* Validate bootloader size */
    if (boot_size != SECTOR_SIZE) {
        fprintf(stderr, "Error: boot.bin must be exactly %d bytes (got %zu bytes)\n", SECTOR_SIZE, boot_size);
        return 1;
    }

    /* Verify boot signature 0x55 0xAA */
    if (boot_data[510] != 0x55 || boot_data[511] != 0xAA) {
        fprintf(stderr, "Error: boot.bin missing 0xAA55 boot signature at byte 510/511\n");
        return 1;
    }

    /* Validate stage2 size */
    size_t max_stage2 = STAGE2_SECTOR_COUNT * SECTOR_SIZE;
    if (stage2_size > max_stage2) {
        fprintf(stderr, "Error: stage2.bin exceeds %zu bytes (got %zu bytes)\n", max_stage2, stage2_size);
        return 1;
    }

    /* Check if kernel has a 1MB zero prefix from PE linking and strip if needed */
    uint8_t *kernel_payload = kernel_data;
    size_t kernel_payload_size = kernel_size;

    if (kernel_size > 0x100000) {
        /* Check if first 1MB is all zeroes */
        int all_zeros = 1;
        for (size_t i = 0; i < 0x100000; i++) {
            if (kernel_data[i] != 0) {
                all_zeros = 0;
                break;
            }
        }
        if (all_zeros) {
            kernel_payload = kernel_data + 0x100000;
            kernel_payload_size = kernel_size - 0x100000;
        }
    }

    /* Allocate disk image memory and initialize with zeros */
    uint8_t *image = (uint8_t *)calloc(1, TOTAL_IMAGE_SIZE);
    if (!image) {
        fprintf(stderr, "Error: Out of memory allocating disk image buffer\n");
        return 1;
    }

    /* 1. Copy Boot Sector (Sector 0) */
    memcpy(image + (BOOT_SECTOR_OFFSET * SECTOR_SIZE), boot_data, boot_size);

    /* 2. Copy Stage 2 (Sectors 1..4) */
    memcpy(image + (STAGE2_SECTOR_OFFSET * SECTOR_SIZE), stage2_data, stage2_size);

    /* 3. Copy Kernel (Sectors 5..) */
    size_t kernel_offset = KERNEL_SECTOR_OFFSET * SECTOR_SIZE;
    if (kernel_offset + kernel_payload_size > TOTAL_IMAGE_SIZE) {
        fprintf(stderr, "Error: Kernel binary exceeds available disk image space\n");
        return 1;
    }
    memcpy(image + kernel_offset, kernel_payload, kernel_payload_size);

    /* Write disk image */
    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "Error: Could not open output file '%s' for writing\n", out_path);
        return 1;
    }

    if (fwrite(image, 1, TOTAL_IMAGE_SIZE, out) != TOTAL_IMAGE_SIZE) {
        fprintf(stderr, "Error: Failed to write complete disk image\n");
        fclose(out);
        return 1;
    }
    fclose(out);

    size_t kernel_sectors = (kernel_payload_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    printf("\n  [IMAGE] %s created successfully (%d KB / %d sectors)\n",
           out_path, TOTAL_IMAGE_SIZE / 1024, TOTAL_SECTORS);
    printf("    Sector 0      (0x%06X): boot.bin   (%zu bytes)\n",
           BOOT_SECTOR_OFFSET * SECTOR_SIZE, boot_size);
    printf("    Sectors 1..4  (0x%06X): stage2.bin (%zu bytes)\n",
           STAGE2_SECTOR_OFFSET * SECTOR_SIZE, stage2_size);
    printf("    Sectors 5..%-2zu (0x%06X): kernel.bin (%zu bytes, %zu sectors)\n\n",
           KERNEL_SECTOR_OFFSET + kernel_sectors - 1, (unsigned int)kernel_offset,
           kernel_payload_size, kernel_sectors);

    free(boot_data);
    free(stage2_data);
    free(kernel_data);
    free(image);

    return 0;
}
