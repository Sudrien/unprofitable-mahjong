#!/usr/bin/env python3
"""
gen_tiles.py -- render the 34 tile faces into main/tilefaces.c.

The faces are the web version's own drawings: the tile-face code is cut
out of ../../index.html (from `const NUM` to the deal) and run in
headless Chromium, so the board shows the same tiles the browser does.
Each face is rendered at each size the firmware draws, rather than
scaled from one, so the small ones stay sharp.

Stored as one shared palette (index 0 is transparent: the rounded
corners) and run-length rows: (count, index) byte pairs, never crossing
a row. The faces are mostly flat colour, so this is a fraction of raw
RGB565 and is drawn straight into gfx's shadow buffer without unpacking
anywhere first.

Needs: Python 3, Pillow, Playwright with Chromium, and Noto Serif CJK
(the web version's tile font is Noto Serif SC; the CJK collection's Bold
has the same glyphs). The output is committed, like ark12.c, so a build
needs none of this.

    python3 m5tab5/tools/gen_tiles.py            # writes m5tab5/main/tilefaces.[ch]
    python3 m5tab5/tools/gen_tiles.py --preview  # also writes tilefaces-preview.png

SPDX-License-Identifier: MIT
"""
import argparse
import asyncio
import io
import os
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
OUT_DIR = os.path.normpath(os.path.join(HERE, '..', 'main'))

# name, width, height: the sizes the firmware draws
SIZES = [('L', 96, 128), ('M', 54, 72), ('S', 27, 36)]
FONT = 'Noto Serif CJK SC'


def face_code():
    html = open(os.path.join(ROOT, 'index.html'), encoding='utf-8').read()
    js = html.split('<script>')[1].split('</script>')[0]
    a = js.index('const NUM')
    b = js.index('/* ---------- solvable deal')
    return js[a:b]


def page_html(code):
    tiles = []
    for name, w, h in SIZES:
        pad = max(1, round(w * 0.07))
        rx = round(w * 0.09)
        ry = round(h * 0.07)
        for k in range(34):
            tiles.append(f'<div class="t" id="{name}{k}" style="width:{w}px;height:{h}px;padding:{pad}px;'
                         f'border-radius:{rx}px/{ry}px" data-k="{k}"></div>')
    return f'''<!doctype html><html><head><meta charset="utf-8"><style>
:root {{ --face:#fbf8ef; --t-red:#b5302a; --t-green:#1e7247; --t-blue:#234f8c; --t-ink:#1b1b1b;
        --f-tile:"{FONT}", serif; }}
body {{ margin:0; background:transparent; display:flex; flex-wrap:wrap; gap:4px; padding:4px; }}
.t {{ box-sizing:border-box; background:var(--face); overflow:hidden; }}
.t svg {{ display:block; width:100%; height:100%; }}
</style></head><body>{''.join(tiles)}
<script>{code}
document.querySelectorAll('.t').forEach(el => {{ el.innerHTML = face(+el.dataset.k); }});
</script></body></html>'''


async def render(html):
    from playwright.async_api import async_playwright
    shots = {}
    async with async_playwright() as p:
        kw = {}
        for exe in ('/opt/pw-browsers/chromium-1194/chrome-linux/chrome',):
            if os.path.exists(exe):
                kw['executable_path'] = exe
        b = await p.chromium.launch(**kw)
        pg = await b.new_page(viewport={'width': 1400, 'height': 1000}, device_scale_factor=1)
        await pg.set_content(html)
        await pg.evaluate('document.fonts.ready')
        ok = await pg.evaluate(f'document.fonts.check("700 20px \\"{FONT}\\"", "萬東發")')
        if not ok:
            sys.exit(f'gen_tiles: font "{FONT}" not found; install Noto Serif CJK')
        for name, w, h in SIZES:
            for k in range(34):
                png = await pg.locator(f'#{name}{k}').screenshot(omit_background=True)
                im = Image.open(io.BytesIO(png)).convert('RGBA')
                if im.size != (w, h):
                    sys.exit(f'gen_tiles: {name}{k} rendered {im.size}, wanted {(w, h)}')
                shots[(name, k)] = im
        await b.close()
    return shots


def pixels(im):
    # Pillow 12 deprecates getdata() in favour of get_flattened_data()
    f = getattr(im, 'get_flattened_data', None)
    return list(f() if f else im.getdata())


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def build(shots):
    # one palette for every face and size: quantize all opaque pixels together
    opaque = []
    for im in shots.values():
        opaque.extend(px[:3] for px in pixels(im) if px[3] >= 128)
    strip = Image.new('RGB', (len(opaque), 1))
    strip.putdata(opaque)
    pal_img = strip.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    flat = pal_img.getpalette()[:255 * 3]
    ncol = len(set(pixels(pal_img)))
    palette = [0] + [rgb565(*flat[i * 3:i * 3 + 3]) for i in range(255)]

    data = {}
    for (name, k), im in shots.items():
        rgb = im.convert('RGB').quantize(palette=pal_img, dither=Image.Dither.NONE)
        idx = pixels(rgb)
        alpha = [px[3] for px in pixels(im)]
        w, h = im.size
        out = bytearray()
        for y in range(h):
            row = [0 if alpha[y * w + x] < 128 else idx[y * w + x] + 1 for x in range(w)]
            x = 0
            while x < w:
                v = row[x]
                n = 1
                while x + n < w and row[x + n] == v and n < 255:
                    n += 1
                out += bytes((n, v))
                x += n
        data[(name, k)] = bytes(out)
    return palette, ncol, data


def decode(rle, w, h):
    out = []
    i = 0
    while i < len(rle):
        out.extend([rle[i + 1]] * rle[i])
        i += 2
    assert len(out) == w * h, (len(out), w, h)
    return out


def write_c(palette, ncol, data, commit):
    hpath = os.path.join(OUT_DIR, 'tilefaces.h')
    cpath = os.path.join(OUT_DIR, 'tilefaces.c')
    total = sum(len(v) for v in data.values())
    head = f'''/*
 * tilefaces -- the 34 tile faces at the sizes the firmware draws.
 *
 * GENERATED FILE. Do not edit. Regenerate with:
 *     python3 m5tab5/tools/gen_tiles.py
 * from index.html ({commit}), rendered in Chromium with Noto Serif CJK
 * Bold for the characters.
 *
 * One RGB565 palette (index 0 is transparent: the rounded corners) and,
 * per face, run-length rows of (count, index) byte pairs that never
 * cross a row. {total} bytes of runs for all three sizes.
 *
 * SPDX-License-Identifier: MIT
 */
'''
    h = head + '''#pragma once
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
'''
    c = [head, '#include "tilefaces.h"\n\n']
    c.append(f'/* {ncol} colours used of 255 */\nconst uint16_t tilefaces_palette[256] = {{\n')
    for i in range(0, 256, 8):
        c.append('    ' + ', '.join(f'0x{v:04X}' for v in palette[i:i + 8]) + ',\n')
    c.append('};\n\n')
    for name, w, hh in SIZES:
        for k in range(34):
            b = data[(name, k)]
            c.append(f'static const uint8_t f{name}{k}[{len(b)}] = {{\n')
            for i in range(0, len(b), 24):
                c.append('    ' + ','.join(str(x) for x in b[i:i + 24]) + ',\n')
            c.append('};\n')
        c.append(f'\nconst tilefaces_size_t tilefaces_{name} = {{ {w}, {hh},\n')
        c.append('    { ' + ', '.join(f'f{name}{k}' for k in range(34)) + ' },\n')
        c.append('    { ' + ', '.join(str(len(data[(name, k)])) for k in range(34)) + ' },\n};\n\n')
    open(hpath, 'w').write(h)
    # one newline at the end, not a blank line: git am warns about those
    open(cpath, 'w').write(''.join(c).rstrip('\n') + '\n')
    return total


def preview(palette, data, path):
    def rgb(v):
        return ((v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3)
    img = Image.new('RGB', (9 * (96 + 4) + 8, 4 * (128 + 72 + 36 + 12) + 8), (220, 228, 220))
    for k in range(34):
        col, rowb = k % 9, k // 9
        y0 = 4 + rowb * (128 + 72 + 36 + 12)
        x0 = 4 + col * 100
        yy = y0
        for name, w, h in SIZES:
            px = decode(data[(name, k)], w, h)
            for i, v in enumerate(px):
                if v:
                    img.putpixel((x0 + i % w, yy + i // w), rgb(palette[v]))
            yy += h + 4
    img.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--preview', action='store_true')
    a = ap.parse_args()
    try:
        # the blob id, not a commit: it names the drawings themselves, and
        # survives git am, which gives every commit a new id
        commit = 'blob ' + subprocess.check_output(['git', '-C', ROOT, 'hash-object', 'index.html'], text=True).strip()[:12]
    except Exception:
        commit = 'unknown'
    shots = asyncio.run(render(page_html(face_code())))
    palette, ncol, data = build(shots)
    for (name, k), b in data.items():
        w, h = {n: (w, h) for n, w, h in SIZES}[name]
        decode(b, w, h)
    total = write_c(palette, ncol, data, commit)
    print(f'gen_tiles: {ncol} colours, {total} bytes of runs -> {OUT_DIR}/tilefaces.[ch]')
    if a.preview:
        p = os.path.join(OUT_DIR, '..', 'tilefaces-preview.png')
        preview(palette, data, p)
        print('gen_tiles: preview', os.path.normpath(p))


if __name__ == '__main__':
    main()
