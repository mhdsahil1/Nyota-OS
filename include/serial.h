#ifndef NYOTA_SERIAL_H
#define NYOTA_SERIAL_H

#include "types.h"

void serial_init(void);
void serial_putchar(char c);
void serial_write(const char *str);
void serial_write_hex(uint64_t val);
void serial_write_dec(uint64_t val);
bool serial_has_data(void);
char serial_getchar(void);

#endif /* NYOTA_SERIAL_H */
