/* =============================================================================
 * Nyota OS — User Space & Syscall Security Validation Suite
 * Tests pointer validation, privilege levels, syscall bounds, and protections.
 * =========================================================================== */

#ifndef NYOTA_USERTEST_H
#define NYOTA_USERTEST_H

#include "types.h"

void usertest_run_all(void);
void usertest_trigger_kernel_write(void);

#endif /* NYOTA_USERTEST_H */
