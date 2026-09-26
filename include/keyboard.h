#ifndef NYOTA_KEYBOARD_H
#define NYOTA_KEYBOARD_H

#include "types.h"

#define KEYBOARD_BUFFER_SIZE 128

/* Special Key Character Constants */
#define KEY_NULL        0
#define KEY_ENTER       '\n'
#define KEY_BACKSPACE   '\b'
#define KEY_TAB         '\t'
#define KEY_ESCAPE      27

/* Non-ASCII navigation and special keys */
typedef enum {
    KEY_NONE      = 0,
    KEY_UP        = 0x80,
    KEY_DOWN      = 0x81,
    KEY_LEFT      = 0x82,
    KEY_RIGHT     = 0x83,
    KEY_HOME      = 0x84,
    KEY_END       = 0x85,
    KEY_PAGE_UP   = 0x86,
    KEY_PAGE_DOWN = 0x87,
    KEY_DELETE    = 0x88,
    KEY_INSERT    = 0x89
} special_key_t;

/* Keyboard Event Structure */
typedef struct {
    uint8_t scancode;
    char    character;     /* Decoded ASCII character or 0 if non-printable */
    uint8_t special;       /* special_key_t value if arrow/nav key */
    bool    pressed;       /* true = Key Down (Make) */
    bool    released;      /* true = Key Up (Break) */
    bool    shift;         /* Shift modifier active */
    bool    ctrl;          /* Ctrl modifier active */
    bool    alt;           /* Alt modifier active */
    bool    caps_lock;     /* Caps Lock active */
} key_event_t;

/* Public Keyboard Driver APIs */
void keyboard_init(void);
bool keyboard_available(void);
bool keyboard_read_event(key_event_t *event);
char keyboard_getchar(void);
int  keyboard_getchar_nonblocking(void);

#endif /* NYOTA_KEYBOARD_H */
