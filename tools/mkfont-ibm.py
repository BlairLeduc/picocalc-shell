#!/usr/bin/env python3
"""mkfont-ibm.py — the DOS flavour's 40-column font (design.md §4.7) from
VileR's Ultimate Oldschool PC Font Pack (CC BY-SA 4.0, int10h.org).

  tools/mkfont-ibm.py Bm437_IBM_BIOS.otb src/core/font-8x10.c > src/core/font-ibm-8x10.c

Needs fontTools (pip install fonttools). The IBM PC BIOS font is 8x8; the
terminal's cells are 8x10 (font.h), so each glyph is drawn in rows 1-8
with a blank row above and below, and the grid stays 40 x 32.

Glyphs 0x20-0x7E are the IBM font's ASCII. Glyphs 0x00-0x1F are the DEC
special graphics the terminal shows for ESC ( 0 (term.c), and EDIT's
boxes use them: each that code page 437 has is taken from the IBM font,
its lines carried into rows 0 and 9 so they join the cells above and
below; the rest (the control pictures, the scan lines but the middle
one, and the not-equal sign) and glyph 0x7F stay the owner's
font-8x10.c's.
"""
import re
import sys

from fontTools.ttLib import TTFont

# DEC special graphics, glyphs 0x00-0x1F, as Unicode; None keeps the
# owner's glyph. LINES are carried into the rows above and below; SHADE
# repeats its pattern.
DEC = [None, 0x2666, 0x2592, None, None, None, None, 0x00B0,
       0x00B1, None, None, 0x2518, 0x2510, 0x250C, 0x2514, 0x253C,
       None, None, 0x2500, None, None, 0x251C, 0x2524, 0x2534,
       0x252C, 0x2502, 0x2264, 0x2265, 0x03C0, None, 0x00A3, 0x00B7]
LINES = {0x2518, 0x2510, 0x250C, 0x2514, 0x253C, 0x2500, 0x251C, 0x2524, 0x2534, 0x252C, 0x2502}
SHADE = 0x2592


def ibm_glyphs(path):
    font = TTFont(path)
    cmap = font.getBestCmap()
    strike = font['EBDT'].strikeData[0]

    def glyph(cp):
        data = strike[cmap[cp]].imageData
        if len(data) != 8:
            raise SystemExit(f'U+{cp:04X}: {len(data)} bytes, not 8x8')
        return list(data)
    return glyph


def owner_glyphs(path):
    text = open(path).read()
    rows = [int(b, 2) for b in re.findall(r'0b([01]{8})', text)]
    if len(rows) != 128 * 10:
        raise SystemExit(f'{path}: {len(rows)} rows, not 128 glyphs of 10')
    return [rows[i * 10:i * 10 + 10] for i in range(128)]


def cell(g8, cp=None):
    if cp == SHADE:
        return [g8[(r - 1) % 8] for r in range(10)]
    top, bottom = (g8[0], g8[7]) if cp in LINES else (0, 0)
    return [top] + g8 + [bottom]


def main():
    ibm = ibm_glyphs(sys.argv[1])
    glyphs = owner_glyphs(sys.argv[2])
    for i, cp in enumerate(DEC):
        if cp is not None:
            glyphs[i] = cell(ibm(cp), cp)
    for c in range(0x20, 0x7F):
        glyphs[c] = cell(ibm(c))

    out = sys.stdout
    out.write("""// font-ibm-8x10.c — the DOS flavour's 40-column font (design.md §4.7).
//
// Made by tools/mkfont-ibm.py from Bm437_IBM_BIOS.otb in VileR's Ultimate
// Oldschool PC Font Pack, (c) 2016-2020 VileR, https://int10h.org/oldschool-pc-fonts/,
// licensed under CC BY-SA 4.0 (https://creativecommons.org/licenses/by-sa/4.0/).
// This adaptation is under the same licence (THIRD-PARTY.md).
//
// The IBM PC BIOS 8x8 glyphs, each drawn in rows 1-8 of the terminal's 8x10
// cell, so the grid stays 40 x 32. Box-drawing glyphs carry their lines into
// rows 0 and 9. The DEC graphics code page 437 lacks, and glyph 0x7F, are the
// owner's font-8x10.c's. Do not edit: run the tool.

#include "font.h"

const font_t font_ibm_8x10 = {
    .width = 8,
    .glyphs = {
""")
    for i, g in enumerate(glyphs):
        out.write(f'        // 0x{i:02X}\n')
        for row in g:
            out.write(f'        0b{row:08b},\n')
    out.write('    },\n};\n')


main()
