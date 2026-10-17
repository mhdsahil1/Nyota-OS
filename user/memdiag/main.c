/* =============================================================================
 * Nyota OS — Memory Diagnostics & Health Tool (memdiag)
 * Heap allocator corruption detection, physical frames, and leak analysis.
 * =========================================================================== */

#include "libnyota.h"

static void print_summary(const mem_diag_info_t *diag) {
    printf("========================================\n");
    printf("       NYOTA MEMORY DIAGNOSTICS         \n");
    printf("========================================\n");
    printf("[Physical Memory (PMM)]\n");
    printf("  Total Frames : %lu (%lu MB)\n", diag->pmm_total_frames, diag->pmm_total_bytes / (1024 * 1024));
    printf("  Used Frames  : %lu (%lu MB)\n", diag->pmm_used_frames, diag->pmm_used_bytes / (1024 * 1024));
    printf("  Free Frames  : %lu (%lu MB)\n", diag->pmm_free_frames, diag->pmm_free_bytes / (1024 * 1024));
    uint64_t pmm_pct = (diag->pmm_total_frames > 0) ? (diag->pmm_used_frames * 100 / diag->pmm_total_frames) : 0;
    printf("  Utilization  : %lu%%\n\n", pmm_pct);

    printf("[Kernel Dynamic Allocator (Heap)]\n");
    printf("  Virtual Range: 0x%lx - 0x%lx\n", diag->heap_start, diag->heap_end);
    printf("  Total Heap   : %lu KB\n", diag->heap_total_bytes / 1024);
    printf("  Used Heap    : %lu KB\n", diag->heap_used_bytes / 1024);
    printf("  Free Heap    : %lu KB\n", diag->heap_free_bytes / 1024);
    uint64_t heap_pct = (diag->heap_total_bytes > 0) ? (diag->heap_used_bytes * 100 / diag->heap_total_bytes) : 0;
    printf("  Utilization  : %lu%%\n\n", heap_pct);

    printf("[Allocator Blocks & Fragmentation]\n");
    printf("  Total Blocks : %u\n", diag->total_blocks);
    printf("  Allocated    : %u\n", diag->alloc_blocks);
    printf("  Free Blocks  : %u\n", diag->free_blocks);
    printf("  Largest Free : %lu KB\n", diag->largest_free_chunk / 1024);
    printf("  Corrupted    : %u %s\n", diag->corrupted_blocks,
           diag->corrupted_blocks == 0 ? "(HEALTHY)" : "(CORRUPTION DETECTED!)");
    printf("========================================\n");
}

int main(int argc, char *argv[]) {
    bool do_verify = false;
    bool do_leaks = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verify") == 0) {
            do_verify = true;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--leaks") == 0) {
            do_leaks = true;
        }
    }

    mem_diag_info_t diag;
    memset(&diag, 0, sizeof(diag));
    if (mem_diag(MEM_DIAG_SUMMARY, &diag) < 0) {
        printf("memdiag: failed to query memory diagnostics\n");
        return 1;
    }

    if (do_verify) {
        printf("[Physical Memory (PMM)] Total Frames: %lu, Free: %lu\n", diag.pmm_total_frames, diag.pmm_free_frames);
        uint32_t stats[4] = {0};
        int corrupted = (int)mem_diag(MEM_DIAG_VERIFY, stats);
        printf("[VERIFY] Checking kernel heap headers, canary magic, and block linkages...\n");
        printf("  Blocks checked : %u\n", stats[0]);
        printf("  Allocated      : %u\n", stats[1]);
        printf("  Free chunks    : %u\n", stats[2]);
        printf("  Corrupted      : %u\n", stats[3]);
        if (corrupted == 0) {
            printf("[OK] Kernel Heap allocator integrity verified: 0 corruptions detected.\n");
            return 0;
        } else {
            printf("[FAIL] Heap corruption detected in %d blocks!\n", corrupted);
            return 2;
        }
    }

    if (do_leaks) {
        printf("[LEAKS] Analyzing kernel memory usage and fragmentation...\n");
        printf("  Heap total : %lu KB\n", diag.heap_total_bytes / 1024);
        printf("  Heap used  : %lu KB\n", diag.heap_used_bytes / 1024);
        printf("  Alloc blocks: %u, Free blocks: %u\n", diag.alloc_blocks, diag.free_blocks);
        if (diag.free_blocks > 0 && diag.largest_free_chunk < (diag.heap_free_bytes / diag.free_blocks) / 2) {
            printf("[WARN] High heap fragmentation detected (largest free block is small).\n");
        } else {
            printf("[OK] Heap fragmentation is within normal parameters.\n");
        }
        return 0;
    }

    print_summary(&diag);
    return 0;
}
