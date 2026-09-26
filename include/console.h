#ifndef NYOTA_CONSOLE_H
#define NYOTA_CONSOLE_H

#include "types.h"

#define CONSOLE_LINE_MAX 128

void console_init(void);
void console_run(void) __attribute__((noreturn));
void console_prompt(void);

#endif /* NYOTA_CONSOLE_H */
