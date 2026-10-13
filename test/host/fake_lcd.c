/* fake_lcd.c — src/port/lcd.h on the host, for test_screen.
 *
 * Models what the panel's controller does with the calls: 480 rows of
 * RGB565 frame memory, a scroll offset (VSCSAD) that sets which memory row
 * shows at panel row 0, and windows that take pixels left to right, top to
 * bottom (hardware-notes.md §4.5, §4.8). A blit that would cross the end
 * of frame memory, or that sends a different number of pixels from its
 * window's size, is counted as a fault. */

#include "fake_lcd.h"

#include <string.h>

#include "config.h"
#include "lcd.h"

#define W SHELL_PANEL_W
#define MEM_H SHELL_PANEL_MEM_H

static uint16_t s_mem[MEM_H][W];
static unsigned s_scroll;
static unsigned s_x, s_y, s_w, s_h, s_n;   /* the open window, in frame memory */

unsigned long fake_lcd_pixels, fake_lcd_faults;
bool fake_lcd_ignore_scroll;

void fake_lcd_reset(uint16_t colour) {
    for (unsigned y = 0; y < MEM_H; y++)
        for (unsigned x = 0; x < W; x++) s_mem[y][x] = colour;
    s_scroll = 0;
    fake_lcd_pixels = fake_lcd_faults = 0;
}

uint16_t fake_lcd_panel(unsigned x, unsigned y) {
    return s_mem[(y + s_scroll) % MEM_H][x];
}

uint32_t lcd_init(void) {
    fake_lcd_reset(0x0000);
    return 75000000u;
}

uint32_t lcd_reapply_baud(void) {
    return 75000000u;
}

void lcd_scroll(int lines) {
    if (fake_lcd_ignore_scroll) return;
    int m = (int)MEM_H;
    s_scroll = (unsigned)((((int)s_scroll + lines) % m + m) % m);
}

void lcd_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t rgb565) {
    for (unsigned j = 0; j < h; j++)
        for (unsigned i = 0; i < w; i++) s_mem[(y + j + s_scroll) % MEM_H][x + i] = rgb565;
    fake_lcd_pixels += (unsigned long)w * h;
}

void lcd_blit_begin(unsigned x, unsigned y, unsigned w, unsigned h) {
    s_x = x;
    s_y = (y + s_scroll) % MEM_H;
    s_w = w;
    s_h = h;
    s_n = 0;
    if (x + w > W || s_y + h > MEM_H) fake_lcd_faults++;   /* a window cannot wrap */
}

void lcd_blit_row(const uint16_t *px, unsigned n) {
    for (unsigned i = 0; i < n; i++, s_n++) {
        unsigned y = s_y + s_n / s_w, x = s_x + s_n % s_w;
        if (s_n >= s_w * s_h || y >= MEM_H) {
            fake_lcd_faults++;
            return;
        }
        s_mem[y][x] = px[i];
    }
    fake_lcd_pixels += n;
}

void lcd_blit_end(void) {
    if (s_n != s_w * s_h) fake_lcd_faults++;
}
