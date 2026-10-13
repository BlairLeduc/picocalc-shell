/* keys.c — key events and UART bytes to the shell's input (keys.h). */

#include "keys.h"

#include "keycodes.h"

void keys_init(keys_t *k) {
    k->ctrl = false;
}

int keys_event(keys_t *k, uint8_t state, uint8_t code) {
    if (code == KEY_MOD_CTRL) {
        if (state == KEY_STATE_PRESSED || state == KEY_STATE_HOLD) k->ctrl = true;
        else if (state == KEY_STATE_RELEASED) k->ctrl = false;
        return KEYS_NONE;
    }
    if (state != KEY_STATE_PRESSED) return KEYS_NONE;

    switch (code) {
    case KEY_MOD_ALT:
    case KEY_MOD_SHL:
    case KEY_MOD_SHR:
    case KEY_MOD_SYM:
    case KEY_CAPS_LOCK:
        return KEYS_NONE;
    case KEY_ENTER:
    case KEY_RETURN:
        return '\r';
    case KEY_ESC:
        return 0x1B;
    default:
        break;
    }
    if (k->ctrl) {
        if (code >= 'a' && code <= 'z') return code - 'a' + 1;
        if (code >= '@' && code <= '_') return code - '@';
    }
    return code;
}

int keys_uart(uint8_t byte) {
    if (byte == '\n') return '\r';
    if (byte == 0x7F) return KEY_BACKSPACE;
    return byte;
}
