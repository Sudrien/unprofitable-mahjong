/*
 * tiledraw.c -- see tiledraw.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "tiledraw.h"

#include "gfx.h"

/* The web version's --edge and --edge-dark. */
#define EDGE_LIGHT  RGB(0xcf, 0xc2, 0xa3)
#define EDGE_DARK   RGB(0xa8, 0x98, 0x7a)

void tiledraw_face(const tilefaces_size_t *sz, int kind, int x, int y, uint16_t tint)
{
    if (kind < 0 || kind >= TILEFACES_KINDS) return;
    uint16_t *fb = gfx_fb();
    const int fw = gfx_w(), fh = gfx_h();
    const uint8_t *r = sz->rle[kind];
    const int len = sz->len[kind];
    int px = 0, py = 0;
    for (int i = 0; i + 1 < len; i += 2) {
        const int n = r[i], v = r[i + 1];
        const int yy = y + py;
        if (v && yy >= 0 && yy < fh) {
            const uint16_t c = tint ? tint : tilefaces_palette[v];
            int x0 = x + px, x1 = x0 + n;
            if (x0 < 0) x0 = 0;
            if (x1 > fw) x1 = fw;
            uint16_t *row = fb + (size_t)yy * (size_t)fw;
            for (int xx = x0; xx < x1; xx++) row[xx] = c;
        }
        px += n;
        if (px >= sz->w) { px = 0; py++; }
    }
}

void tiledraw_tile(const tilefaces_size_t *sz, int kind, int x, int y, int edge)
{
    /* the silhouette twice, darker lowest, then the face over both */
    tiledraw_face(sz, kind, x, y + edge, EDGE_DARK);
    tiledraw_face(sz, kind, x, y + edge - (edge > 2 ? 2 : 1), EDGE_LIGHT);
    tiledraw_face(sz, kind, x, y, 0);
}

void tiledraw_ring(const tilefaces_size_t *sz, int x, int y, int edge, int t, uint16_t c)
{
    const int w = sz->w + 2 * t;
    gfx_fill_rect(x - t, y - t, w, t, c);
    gfx_fill_rect(x - t, y + sz->h + edge, w, t, c);
    gfx_fill_rect(x - t, y, t, sz->h + edge, c);
    gfx_fill_rect(x + sz->w, y, t, sz->h + edge, c);
}
