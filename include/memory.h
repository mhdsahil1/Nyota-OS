#ifndef NYOTA_MEMORY_H
#define NYOTA_MEMORY_H

#include "types.h"

void *memset(void *dest, int val, size_t count);
void *memcpy(void *dest, const void *src, size_t count);
void *memmove(void *dest, const void *src, size_t count);
int memcmp(const void *s1, const void *s2, size_t count);

size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

#endif /* NYOTA_MEMORY_H */
