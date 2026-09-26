/* =============================================================================
 * Nyota OS — Memory Subsystem Verification & Stress Tests
 * Tests PMM, Paging, and Heap allocation, alignment, expansion, and freeing.
 * =========================================================================== */

#ifndef NYOTA_MEMTEST_H
#define NYOTA_MEMTEST_H

#include "types.h"

void memtest_run_all(void);
void memtest_trigger_page_fault(void);

#endif /* NYOTA_MEMTEST_H */
