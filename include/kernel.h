#ifndef NYOTA_KERNEL_H
#define NYOTA_KERNEL_H

#include "types.h"

#define NYOTA_OS_NAME       "Nyota OS"
#define NYOTA_OS_VERSION    "0.4.0"
#define NYOTA_ARCH          "x86_64"

/* Basic console text output */
void kprint(const char *str);
void kprintln(const char *str);

/* Logging interface with status tags */
void klog(const char *str);
void kinfo(const char *str);
void kwarn(const char *str);
void kerror(const char *str);

/* Kernel panic handler */
void kernel_panic(const char *reason) __attribute__((noreturn));

#endif /* NYOTA_KERNEL_H */
