#ifndef NYOTA_KERNEL_H
#define NYOTA_KERNEL_H

#include "types.h"

#define NYOTA_OS_NAME       "Nyota OS"
#define NYOTA_OS_VERSION    "0.9.0"
#define NYOTA_ARCH          "x86_64"

/* Basic console text output */
void kprint(const char *str);
void kprintln(const char *str);

/* Logging interface with status tags */
void klog(const char *str);
void kinfo(const char *str);
void kwarn(const char *str);
void kerror(const char *str);

/* Log Levels */
#define KLOG_LEVEL_DEBUG    0
#define KLOG_LEVEL_INFO     1
#define KLOG_LEVEL_WARN     2
#define KLOG_LEVEL_ERROR    3
#define KLOG_LEVEL_CRIT     4

/* Kernel log ring buffer APIs */
void klog_init(void);
void klog_write(uint32_t level, const char *msg);
int klog_read_entries(char *buf, size_t buf_sz, bool clear);

/* Kernel panic handler */
void kernel_panic(const char *reason) __attribute__((noreturn));

#endif /* NYOTA_KERNEL_H */
