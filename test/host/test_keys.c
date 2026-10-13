/* test_keys.c — southbridge events and UART bytes to input (keys.h;
 * hardware-notes.md §6.2, §6.3). */

#include "keycodes.h"
#include "keys.h"
#include "test_util.h"

int main(void) {
    keys_t k;
    keys_init(&k);

    CHECK(keys_event(&k, KEY_STATE_PRESSED, 'a') == 'a', "a press types");
    CHECK(keys_event(&k, KEY_STATE_RELEASED, 'a') == KEYS_NONE, "a release does not");
    CHECK(keys_event(&k, KEY_STATE_HOLD, 'a') == KEYS_NONE, "nor a hold");
    CHECK(keys_event(&k, KEY_STATE_RELEASED, ' ') == KEYS_NONE, "a release with no press");
    CHECK(keys_event(&k, KEY_STATE_PRESSED, 'j') == 'j' && keys_event(&k, KEY_STATE_PRESSED, 'j') == 'j',
          "auto-repeat arrives as more presses, and each types");

    CHECK(keys_event(&k, KEY_STATE_PRESSED, KEY_ENTER) == '\r', "Enter is CR");
    CHECK(keys_event(&k, KEY_STATE_PRESSED, KEY_BACKSPACE) == 0x08, "Backspace");
    CHECK(keys_event(&k, KEY_STATE_PRESSED, KEY_ESC) == 0x1B, "Esc is ESC");
    CHECK(keys_event(&k, KEY_STATE_PRESSED, KEY_CAPS_LOCK) == KEYS_NONE, "Caps Lock is accepted");
    CHECK(keys_event(&k, KEY_STATE_PRESSED, KEY_MOD_SHL) == KEYS_NONE, "Shift alone types nothing");
    CHECK(keys_event(&k, KEY_STATE_PRESSED, KEY_UP) == KEY_UP, "arrows pass as their codes");
    CHECK(keys_event(&k, KEY_STATE_PRESSED, KEY_BREAK) == KEY_BREAK, "Break");

    /* Ctrl arrives as its own event beside the key (§6.3). */
    CHECK(keys_event(&k, KEY_STATE_PRESSED, KEY_MOD_CTRL) == KEYS_NONE, "Ctrl alone");
    CHECK(keys_event(&k, KEY_STATE_PRESSED, 'c') == 0x03, "Ctrl-C");
    CHECK(keys_event(&k, KEY_STATE_PRESSED, 'C') == 0x03, "Ctrl-C with Caps Lock");
    CHECK(keys_event(&k, KEY_STATE_PRESSED, '[') == 0x1B, "Ctrl-[");
    CHECK(keys_event(&k, KEY_STATE_PRESSED, '1') == '1', "Ctrl-1 is 1");
    CHECK(keys_event(&k, KEY_STATE_HOLD, KEY_MOD_CTRL) == KEYS_NONE && k.ctrl, "Ctrl held");
    CHECK(keys_event(&k, KEY_STATE_RELEASED, KEY_MOD_CTRL) == KEYS_NONE, "Ctrl up");
    CHECK(keys_event(&k, KEY_STATE_PRESSED, 'c') == 'c', "c after Ctrl is let go");

    CHECK(keys_uart('\r') == '\r' && keys_uart('\n') == '\r', "UART CR and LF are Enter");
    CHECK(keys_uart(0x7F) == 0x08 && keys_uart(0x08) == 0x08, "UART DEL and BS are Backspace");
    CHECK(keys_uart(0x81) == KEY_F1 && keys_uart('x') == 'x', "UART bytes are themselves");

    TEST_DONE();
}
