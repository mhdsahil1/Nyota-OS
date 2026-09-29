/* =============================================================================
 * Nyota OS — Userspace String & Memory Operations (string.c)
 * Standard freestanding implementations for Ring 3 programs.
 * =========================================================================== */

#include "libnyota.h"

size_t strlen(const char *s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

int strcmp(const char *s1, const char *s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;

    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) return 0;
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;

    while (n-- > 0 && *s1 && (*s1 == *s2)) {
        if (n == 0) return 0;
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strcpy(char *dest, const char *src) {
    if (!dest || !src) return dest;
    char *d = dest;
    while ((*d++ = *src++) != '\0') {
        /* loop */
    }
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }
    return s;
}

char *strchr(const char *s, int c) {
    if (!s) return NULL;
    while (*s != '\0') {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

char *strcat(char *dest, const char *src) {
    if (!dest || !src) return dest;
    char *d = dest;
    while (*d != '\0') d++;
    while ((*d++ = *src++) != '\0') {}
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    char *d = dest;
    while (*d != '\0') d++;
    while (n-- > 0 && *src != '\0') {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

int atoi(const char *s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    int sign = 1;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    int res = 0;
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res * sign;
}
