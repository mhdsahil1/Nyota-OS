/* =============================================================================
 * Nyota OS — User Programs Suite (Phase 5)
 * Position-independent user-space programs for multitasking, preemption, and sleep.
 * =========================================================================== */

#ifndef NYOTA_USER_PROGRAMS_H
#define NYOTA_USER_PROGRAMS_H

#include "types.h"

#define USER_PROGRAM_BLOB_SIZE 256

const void *user_get_prog_a(void);
const void *user_get_prog_b(void);
const void *user_get_prog_c(void);
const void *user_get_prog_bad(void);

#endif /* NYOTA_USER_PROGRAMS_H */
