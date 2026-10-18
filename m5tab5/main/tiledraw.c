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

void tiledraw_face_scaled(const tilefaces_size_t *sz, int kind, int x, int y, int w, int h, uint16_t tint)
{
    if (kind < 0 || kind >= TILEFACES_KINDS || w <= 0 || h <= 0 || sz->w > 128) return;
    uint16_t *fb = gfx_fb();
    const int fw = gfx_w(), fh = gfx_h();
    const uint8_t *r = sz->rle[kind];
    const int len = sz->len[kind];
    uint8_t line[128];         /* one source row, as palette indexes */
    int at = 0, row = -1;      /* where in the RLE the next source row starts, and which row that is */
    for (int dy = 0; dy < h; dy++) {
        const int yy = y + dy, sy = dy * sz->h / h;
        /* walk the RLE forward to source row sy; rows only ever advance */
        while (row < sy && at + 1 < len) {
            int px = 0;
            while (px < sz->w && at + 1 < len) {
                const int n = r[at], v = r[at + 1];
                for (int i = 0; i < n && px + i < sz->w; i++) line[px + i] = (uint8_t)v;
                px += n;
                at += 2;
            }
            row++;
        }
        if (yy < 0 || yy >= fh) continue;
        uint16_t *dst = fb + (size_t)yy * (size_t)fw;
        for (int dx = 0; dx < w; dx++) {
            const int xx = x + dx, v = line[dx * sz->w / w];
            if (v && xx >= 0 && xx < fw) dst[xx] = tint ? tint : tilefaces_palette[v];
        }
    }
}

void tiledraw_tile_scaled(const tilefaces_size_t *sz, int kind, int x, int y, int w, int h, int edge)
{
    tiledraw_face_scaled(sz, kind, x, y + edge, w, h, EDGE_DARK);
    tiledraw_face_scaled(sz, kind, x, y + edge - (edge > 2 ? 2 : 1), w, h, EDGE_LIGHT);
    tiledraw_face_scaled(sz, kind, x, y, w, h, 0);
}
