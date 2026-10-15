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

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
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

char *strrchr(const char *s, int c) {
    if (!s) return NULL;
    const char *last = NULL;
    while (*s != '\0') {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == '\0') return (char *)s;
    return (char *)last;
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

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char *)haystack;
    size_t nlen = strlen(needle);
    while (*haystack) {
        if (strncmp(haystack, needle, nlen) == 0) {
            return (char *)haystack;
        }
        haystack++;
    }
    return NULL;
}

int isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
}

int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

int isalpha(int c) {
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

int isalnum(int c) {
    return (isalpha(c) || isdigit(c));
}

int isprint(int c) {
    return (c >= 0x20 && c <= 0x7E);
}

int isupper(int c) {
    return (c >= 'A' && c <= 'Z');
}

int islower(int c) {
    return (c >= 'a' && c <= 'z');
}

int toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    return c;
}

int tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    if (n == 0 || s1 == s2) return 0;
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return (int)p1[i] - (int)p2[i];
        }
    }
    return 0;
}

char *strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

char *strndup(const char *s, size_t n) {
    if (!s) return NULL;
    size_t len = 0;
    while (len < n && s[len] != '\0') len++;
    char *copy = (char *)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
    if (!saveptr) return NULL;
    char *s = str ? str : *saveptr;
    if (!s) return NULL;

    while (*s && strchr(delim, *s)) s++;
    if (!*s) {
        *saveptr = NULL;
        return NULL;
    }

    char *start = s;
    while (*s && !strchr(delim, *s)) s++;
    if (*s) {
        *s = '\0';
        *saveptr = s + 1;
    } else {
        *saveptr = NULL;
    }
    return start;
}

static char *strtok_saved = NULL;
char *strtok(char *str, const char *delim) {
    return strtok_r(str, delim, &strtok_saved);
}

size_t strspn(const char *s, const char *accept) {
    if (!s || !accept) return 0;
    size_t count = 0;
    while (*s && strchr(accept, *s)) {
        count++;
        s++;
    }
    return count;
}

size_t strcspn(const char *s, const char *reject) {
    if (!s || !reject) return 0;
    size_t count = 0;
    while (*s && !strchr(reject, *s)) {
        count++;
        s++;
    }
    return count;
}

long strtol(const char *nptr, char **endptr, int base) {
    if (!nptr) return 0;
    const char *s = nptr;
    while (isspace((unsigned char)*s)) s++;

    int sign = 1;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    } else if (base == 0 && s[0] == '0') {
        base = 8;
        s++;
    } else if (base == 0) {
        base = 10;
    }

    long result = 0;
    const char *start = s;
    while (*s) {
        int val = -1;
        if (*s >= '0' && *s <= '9') val = *s - '0';
        else if (*s >= 'a' && *s <= 'z') val = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') val = *s - 'A' + 10;
        if (val < 0 || val >= base) break;
        result = result * base + val;
        s++;
    }

    if (endptr) {
        *endptr = (char *)(s == start ? nptr : s);
    }
    return result * sign;
}

long atol(const char *s) {
    return strtol(s, NULL, 10);
}


