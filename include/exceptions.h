#ifndef NYOTA_EXCEPTIONS_H
#define NYOTA_EXCEPTIONS_H

#include "types.h"
#include "interrupts.h"

void exceptions_init(void);
void exception_handler(interrupt_frame_t *frame);
void panic_with_frame(const char *title, const interrupt_frame_t *frame) __attribute__((noreturn));

#endif /* NYOTA_EXCEPTIONS_H */
