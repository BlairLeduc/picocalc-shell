/* fake_lcd.h — the host's model of the panel behind src/port/lcd.h. */
#ifndef SHELL_FAKE_LCD_H
#define SHELL_FAKE_LCD_H

#include <stdbool.h>
#include <stdint.h>

/* Fill all of frame memory with one colour and put the offset at 0. */
void fake_lcd_reset(uint16_t colour);

/* The pixel the panel shows at (x, y), through the scroll offset. */
uint16_t fake_lcd_panel(unsigned x, unsigned y);

/* Pixels sent since the reset, and blits that broke the rules. */
extern unsigned long fake_lcd_pixels, fake_lcd_faults;

/* The control: a panel that ignores lcd_scroll(). */
extern bool fake_lcd_ignore_scroll;

#endif /* SHELL_FAKE_LCD_H */
