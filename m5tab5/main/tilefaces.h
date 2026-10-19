/*
 * tilefaces -- the 34 tile faces at the sizes the firmware draws.
 *
 * GENERATED FILE. Do not edit. Regenerate with:
 *     python3 m5tab5/tools/gen_tiles.py
 * from index.html (blob dcf3c1bdcaf0), rendered in Chromium with Noto Serif CJK
 * Bold for the characters.
 *
 * The code and the drawings are MIT. The characters (the numbers, 萬,
 * the winds and two of the dragons) are pictures of Noto Serif CJK
 * glyphs: (c) 2017 Adobe, https://github.com/notofonts/noto-cjk, under
 * the SIL Open Font License 1.1. The OFL covers the font, not what is
 * drawn with it; this says where they come from.
 *
 * One RGB565 palette (index 0 is transparent: the rounded corners) and,
 * per face, run-length rows of (count, index) byte pairs that never
 * cross a row. 119836 bytes of runs for all three sizes.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdint.h>

#define TILEFACES_KINDS (34)

typedef struct {
    uint16_t w, h;
    const uint8_t *rle[TILEFACES_KINDS];
    uint16_t len[TILEFACES_KINDS];      /* bytes of runs */
} tilefaces_size_t;

extern const uint16_t tilefaces_palette[256];
extern const tilefaces_size_t tilefaces_L;   /* the hand */
extern const tilefaces_size_t tilefaces_M;   /* melds, the celebration */
extern const tilefaces_size_t tilefaces_S;   /* the rail */
