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

/* Chords, while keys_report_chords is on (EDIT's menus, selection and
 * word moves, design.md §4.7): KEYS_ALT with a lower-case letter or an
 * arrow; KEYS_ALT_TAP for Alt pressed and let go with no key between and
 * before the MCU's hold events began (hardware-notes.md §6.3); KEYS_CTRL
 * with an arrow, Home or End. Off, a chord is the key the MCU sent, as at
 * the prompt. */
#define KEYS_ALT     0x100
#define KEYS_CTRL    0x200
#define KEYS_ALT_TAP (KEYS_ALT | 0xA1)

typedef struct {
    bool ctrl;       /* Ctrl is down */
    bool alt;        /* Alt is down */
    bool alt_alone;  /* ... and nothing else, not even its hold, since */
    bool uart_alt;   /* the UART's US: the next byte is with Alt */
} keys_t;

void keys_init(keys_t *k);

/* Turn the reporting of chords on or off: one setting for every keys_t,
 * since there is one keyboard. */
void keys_report_chords(bool on);

/* One FIFO event [state, code]. Returns the input byte, or KEYS_NONE.
 * Only presses type: the MCU's auto-repeat arrives as more presses, and
 * releases, including one with no press before it, are ignored
 * (hardware-notes.md §6.2). Ctrl passes the key through beside its own
 * event (§6.3), so Ctrl with a letter or one of @[\]^_ is its control
 * code. Enter is CR, Esc is ESC, Caps Lock is accepted and types
 * nothing (the MCU applies it to the letters). */
int keys_event(keys_t *k, uint8_t state, uint8_t code);

/* One byte received on the UART: CR and LF are Enter (CR), DEL is
 * Backspace, and the rest is itself, so \x81 is F1 as from the keyboard.
 * US (0x1F) puts Alt on the next byte while chords are reported, and US US is
 * a tap of Alt; otherwise US is itself. Returns KEYS_NONE for a US that
 * waits for its byte. */
int keys_uart(keys_t *k, uint8_t byte);

#endif /* SHELL_KEYS_H */
