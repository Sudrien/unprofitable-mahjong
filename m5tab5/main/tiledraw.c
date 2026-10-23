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

void tiledraw_image_scaled(const uint16_t *src, int sw, int sh, int x, int y, int w, int h)
{
    if (!src || sw <= 0 || sh <= 0 || w <= 0 || h <= 0) return;
    uint16_t *fb = gfx_fb();
    const int fw = gfx_w(), fh = gfx_h();
    for (int dy = 0; dy < h; dy++) {
        const int yy = y + dy;
        if (yy < 0 || yy >= fh) continue;
        int y0 = dy * sh / h, y1 = (dy + 1) * sh / h;
        if (y1 <= y0) y1 = y0 + 1;
        uint16_t *dst = fb + (size_t)yy * (size_t)fw;
        for (int dx = 0; dx < w; dx++) {
            const int xx = x + dx;
            if (xx < 0 || xx >= fw) continue;
            int x0 = dx * sw / w, x1 = (dx + 1) * sw / w;
            if (x1 <= x0) x1 = x0 + 1;
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < y1; sy++)
                for (int sx = x0; sx < x1; sx++) {
                    const uint16_t c = src[sy * sw + sx];
                    r += c >> 11; g += (c >> 5) & 0x3f; b += c & 0x1f; n++;
                }
            dst[xx] = (uint16_t)(((r / n) << 11) | ((g / n) << 5) | (b / n));
        }
    }
}
