/* keys.h — key events and UART bytes to the shell's input (design.md §4.1).
 *
 * The input is a stream of bytes: below 0x80 a character, 0x80 and above
 * a key from keycodes.h. Keys come from two places, the southbridge's
 * FIFO and, in the development build, the UART (EL §13.1). */
#ifndef SHELL_KEYS_H
#define SHELL_KEYS_H

#include <stdbool.h>
#include <stdint.h>

#define KEYS_NONE (-1)

typedef struct {
    bool ctrl;   /* Ctrl is down */
} keys_t;

void keys_init(keys_t *k);

/* One FIFO event [state, code]. Returns the input byte, or KEYS_NONE.
 * Only presses type: the MCU's auto-repeat arrives as more presses, and
 * releases, including one with no press before it, are ignored
 * (hardware-notes.md §6.2). Ctrl passes the key through beside its own
 * event (§6.3), so Ctrl with a letter or one of @[\]^_ is its control
 * code. Enter is CR, Esc is ESC, Caps Lock is accepted and types
 * nothing (the MCU applies it to the letters). */
int keys_event(keys_t *k, uint8_t state, uint8_t code);

/* One byte received on the UART: CR and LF are Enter (CR), DEL is
 * Backspace, and the rest is itself, so \x81 is F1 as from the keyboard. */
int keys_uart(uint8_t byte);

#endif /* SHELL_KEYS_H */
