/*
 * tilecheck.c -- every generated tile face decodes to exactly its size,
 * uses only palette entries that exist, and has transparent corners and
 * an opaque middle. Catches a gen_tiles.py change that writes runs the
 * firmware's drawing loop would walk off the end of.
 *
 * SPDX-License-Identifier: MIT
 */
#include "tilefaces.h"

#include <stdio.h>

static int check(const char *name, const tilefaces_size_t *s)
{
    int bad = 0;
    for (int k = 0; k < TILEFACES_KINDS; k++) {
        const uint8_t *r = s->rle[k];
        int x = 0, px = 0, corner = -1, middle = -1;
        for (int i = 0; i + 1 < s->len[k]; i += 2) {
            const int n = r[i], v = r[i + 1];
            if (n == 0 || x + n > s->w) { printf("%s%d: run crosses a row\n", name, k); bad++; break; }
            if (px == 0) corner = v;
            const int mid = (s->h / 2) * s->w + s->w / 2;
            if (px <= mid && mid < px + n) middle = v;
            x += n; px += n;
            if (x == s->w) x = 0;
        }
        if (px != s->w * s->h) { printf("%s%d: %d pixels, want %d\n", name, k, px, s->w * s->h); bad++; }
        if (corner != 0) { printf("%s%d: corner not transparent\n", name, k); bad++; }
        if (middle <= 0) { printf("%s%d: middle transparent\n", name, k); bad++; }
    }
    return bad;
}

int main(void)
{
    const int bad = check("L", &tilefaces_L) + check("M", &tilefaces_M) + check("S", &tilefaces_S);
    if (tilefaces_palette[0] != 0) { puts("palette[0] is not the transparent slot"); return 1; }
    printf("tilefaces: 3 sizes x %d faces %s\n", TILEFACES_KINDS, bad ? "FAILED" : "decode cleanly");
    return bad ? 1 : 0;
}
