/* =============================================================================
 * Nyota OS — Userspace Standard I/O (io.c)
 * Minimal buffered I/O, format printing, and line reading for Ring 3 binaries.
 * =========================================================================== */

#include "libnyota.h"

typedef __builtin_va_list va_list;
#define va_start(v, l)  __builtin_va_start(v, l)
#define va_end(v)       __builtin_va_end(v)
#define va_arg(v, l)    __builtin_va_arg(v, l)

int putchar(int c) {
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
    return c;
}

int puts(const char *s) {
    if (!s) return 0;
    write(STDOUT_FILENO, s, strlen(s));
    putchar('\n');
    return 0;
}

int getchar(void) {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) <= 0) {
        return -1;
    }
    return (int)(unsigned char)c;
}

int getline(char *buf, size_t size) {
    if (!buf || size == 0) return -1;

    size_t i = 0;
    while (i < size - 1) {
        char c = 0;
        int64_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            if (i == 0) return -1;
            break;
        }

        if (c == '\n' || c == '\r') {
            break;
        }
        buf[i++] = c;
    }

    buf[i] = '\0';
    return (int)i;
}

static void print_dec_padded(int64_t val, int min_width, char pad_char) {
    if (val < 0) {
        putchar('-');
        val = -val;
        if (min_width > 0) min_width--;
    }

    char buf[24];
    int i = 0;
    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            buf[i++] = (char)('0' + (val % 10));
            val /= 10;
        }
    }

    while (min_width > i) {
        putchar(pad_char);
        min_width--;
    }

    for (int j = i - 1; j >= 0; j--) {
        putchar(buf[j]);
    }
}

static void print_dec(int64_t val) {
    print_dec_padded(val, 0, ' ');
}

static void print_hex(uint64_t val) {
    putchar('0');
    putchar('x');
    if (val == 0) {
        putchar('0');
        return;
    }

    char buf[16];
    int i = 0;
    const char *hex_digits = "0123456789abcdef";
    while (val > 0) {
        buf[i++] = hex_digits[val & 0x0F];
        val >>= 4;
    }

    for (int j = i - 1; j >= 0; j--) {
        putchar(buf[j]);
    }
}

int printf(const char *fmt, ...) {
    if (!fmt) return 0;

    va_list args;
    va_start(args, fmt);

    int count = 0;
    for (size_t i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] == '%' && fmt[i + 1] != '\0') {
            i++;

            char pad_char = ' ';
            if (fmt[i] == '0') {
                pad_char = '0';
                i++;
            }

            int width = 0;
            while (fmt[i] >= '0' && fmt[i] <= '9') {
                width = width * 10 + (fmt[i] - '0');
                i++;
            }

            /* Optional long prefix (e.g. %ld, %llu) */
            if (fmt[i] == 'l') {
                i++;
                if (fmt[i] == 'l') i++;
            }

            switch (fmt[i]) {
                case 's': {
                    const char *s = va_arg(args, const char *);
                    if (!s) s = "(null)";
                    write(STDOUT_FILENO, s, strlen(s));
                    break;
                }
                case 'd':
                case 'i': {
                    int val = va_arg(args, int);
                    print_dec_padded((int64_t)val, width, pad_char);
                    break;
                }
                case 'u': {
                    unsigned int val = va_arg(args, unsigned int);
                    print_dec_padded((int64_t)val, width, pad_char);
                    break;
                }
                case 'x':
                case 'p': {
                    uint64_t val = va_arg(args, uint64_t);
                    print_hex(val);
                    break;
                }
                case 'c': {
                    int c = va_arg(args, int);
                    putchar(c);
                    break;
                }
                case '%': {
                    putchar('%');
                    break;
                }
                default:
                    putchar('%');
                    putchar(fmt[i]);
                    break;
            }
        } else {
            putchar(fmt[i]);
            count++;
        }
    }

    va_end(args);
    return count;
}
