/* kbd.h — southbridge key events (design.md §4.1; hardware-notes.md §6).
 *
 * After pico-oric's src/port/kbd.h (GPL-3.0, the owner's), without its
 * cross-core ring: the shell runs on one core, so the poll and the reader
 * share a plain ring (design.md §5). */
#ifndef SHELL_KBD_H
#define SHELL_KBD_H

#include <stdbool.h>
#include <stdint.h>

/* From the main loop at about 30 Hz (hardware-notes.md §6.1): drain the
 * whole FIFO, up to its 31 entries, into the ring. Each read costs about
 * 5 ms. Returns the events read. */
unsigned kbd_poll(void);

/* Take one event, if there is one. */
bool kbd_pop(uint8_t *state, uint8_t *code);

/* Events the ring had no room for: zero unless nothing reads it. */
uint32_t kbd_overflows(void);

#endif /* SHELL_KBD_H */
