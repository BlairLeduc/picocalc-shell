// From references/fonts/font.h, the owner's own font, copied unchanged (design.md §4.1),
// but for font_ibm_8x10's declaration, the DOS flavour's (§4.7, font-ibm-8x10.c).
#pragma once

#include <stdint.h>

#define GLYPH_HEIGHT 10 // Height of each glyph in pixels

typedef struct {
    uint8_t width;
    uint8_t glyphs[];
} font_t;

extern const font_t font_8x10; // 8x10 pixel font
extern const font_t font_5x10; // 5x10 pixel font
extern const font_t font_ibm_8x10; // the DOS flavour's 8x10: IBM's 8x8 (font-ibm-8x10.c)
