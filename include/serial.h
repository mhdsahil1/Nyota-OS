#ifndef NYOTA_SERIAL_H
#define NYOTA_SERIAL_H

#include "types.h"

void serial_init(void);
void serial_putchar(char c);
void serial_write(const char *str);
bool serial_has_data(void);
char serial_getchar(void);

#endif /* NYOTA_SERIAL_H */
