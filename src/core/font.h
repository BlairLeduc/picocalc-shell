// From references/fonts/font.h, the owner's own, but for the fonts it declares:
// each flavour's 8x10 (design.md §4.1, §4.7).
#pragma once

#include <stdint.h>

#define GLYPH_HEIGHT 10 // Height of each glyph in pixels

typedef struct {
    uint8_t width;
    uint8_t glyphs[];
} font_t;

extern const font_t font_bbc_master_512; // the UNIX flavour's: the Master 512's (font-master-512-8x10.c)
extern const font_t font_ibm_8x10; // the DOS flavour's 8x10: IBM's 8x8 (font-ibm-8x10.c)
