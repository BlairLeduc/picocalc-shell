/* card.h — the SD card's slot (design.md §4.2, §8).
 *
 * The shell holds one FatFs mount for as long as the card stays in. The
 * main loop polls card detect, so a card taken out, or changed, at the
 * prompt is mounted afresh at the next command that touches it.
 */
#ifndef SHELL_CARD_H
#define SHELL_CARD_H

void card_poll(void);

#endif /* SHELL_CARD_H */
