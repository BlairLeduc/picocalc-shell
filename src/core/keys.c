/* keys.c — key events and UART bytes to the shell's input (keys.h). */

#include "keys.h"

#include "keycodes.h"

#define UART_ALT 0x1F   /* US */

static bool s_report;

void keys_init(keys_t *k) {
    k->ctrl = k->alt = k->alt_alone = k->uart_alt = false;
}

void keys_report_chords(bool on) {
    s_report = on;
}

/* With Alt down: a letter, lower-cased (the MCU sends a capital), or an
 * arrow, as an Alt chord; anything else as itself. */
static int with_alt(int code) {
    if (code >= 'A' && code <= 'Z') return KEYS_ALT | (code - 'A' + 'a');
    if (code >= 'a' && code <= 'z') return KEYS_ALT | code;
    if (code >= KEY_LEFT && code <= KEY_RIGHT) return KEYS_ALT | code;
    return code;
}

int keys_event(keys_t *k, uint8_t state, uint8_t code) {
    if (code == KEY_MOD_CTRL) {
        if (state == KEY_STATE_PRESSED || state == KEY_STATE_HOLD) k->ctrl = true;
        else if (state == KEY_STATE_RELEASED) k->ctrl = false;
        return KEYS_NONE;
    }
    if (code == KEY_MOD_ALT) {
        if (state == KEY_STATE_PRESSED) {
            k->alt = k->alt_alone = true;
        } else if (state == KEY_STATE_HOLD) {
            k->alt = true;
            k->alt_alone = false;   /* held, not tapped */
        } else if (state == KEY_STATE_RELEASED) {
            bool tap = k->alt && k->alt_alone;
            k->alt = k->alt_alone = false;
            if (tap && s_report) return KEYS_ALT_TAP;
        }
        return KEYS_NONE;
    }
    if (state != KEY_STATE_PRESSED) return KEYS_NONE;
    k->alt_alone = false;

    switch (code) {
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
        bool move = (code >= KEY_LEFT && code <= KEY_RIGHT) || code == KEY_HOME || code == KEY_END;
        if (move && s_report) return KEYS_CTRL | code;
        if (code >= 'a' && code <= 'z') return code - 'a' + 1;
        if (code >= '@' && code <= '_') return code - '@';
    }
    if (k->alt && s_report) return with_alt(code);
    return code;
}

int keys_uart(keys_t *k, uint8_t byte) {
    if (k->uart_alt) {
        k->uart_alt = false;
        if (byte == UART_ALT) return KEYS_ALT_TAP;
        return with_alt(keys_uart(k, byte));
    }
    if (byte == UART_ALT && s_report) {
        k->uart_alt = true;
        return KEYS_NONE;
    }
    if (byte == '\n') return '\r';
    if (byte == 0x7F) return KEY_BACKSPACE;
    return byte;
}
