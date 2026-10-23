#!/usr/bin/env python3
"""
icons.py -- draw the page's icons: the 1 Bamboo, as index.html's face(18)
draws it, on its ivory face with the two edge shades under it.

    python3 tools/icons.py          # from the repository root

writes

    apple-touch-icon.png      180x180, the same tile on the table's surface:
                              opaque, since iOS paints transparency black,
                              and clear of the corners iOS rounds off
    icons/icon-192.png        the web app manifest's icons: the tile alone,
    icons/icon-512.png        no background, a block of a real tile's
                              proportions lying on its side, seen from
                              in front and above
    icons/maskable-512.png    the same tile on the table's surface,
                              small enough to survive any mask (all of
                              it inside the central circle)

and rewrites the two favicon <link>s in index.html, which are data URIs so
the page still works opened from disk: an SVG, and a 32x32 PNG for
browsers that take no SVG favicon.

The perspective tile is also the replay QR code's centre icon, on both
sides, so the code a phone sees carries the app's own icon:

    QR_ICON in index.html     a 160x160 PNG data URI, transparent round
                              the tile, drawn by showQr over a white square
    m5tab5/main/qricon.[ch]   the same tile at QRICON_PX square, on white,
                              as RGB565 for the Tab5's Share view

Needs cairosvg and Pillow (pip install cairosvg pillow).

SPDX-License-Identifier: MIT
"""
import base64
import io
import math
import os
import re
import sys
import urllib.parse

import cairosvg
from PIL import Image

# the light theme's --face, --edge, --edge-dark, --t-green, --t-red and
# --surface, from index.html
FACE, EDGE, EDGE_DARK, GREEN, RED = '#fbf8ef', '#cfc2a3', '#a8987a', '#1e7247', '#b5302a'
SURFACE = '#d3ddd0'
# the dark theme's --bg: the back of the PWA tile, and its outline
DARK_BG = '#111a1b'

# face(18) in index.html, the 60x80 tile, with stick() written out
BAMBOO_1 = (
    f'<rect x="24.5" y="11" width="11" height="58" rx="5.5" fill="{GREEN}"/>'
    f'<rect x="25.7" y="39.2" width="8.6" height="1.6" fill="{FACE}"/>'
    f'<rect x="25.5" y="21" width="9" height="1.8" fill="{FACE}"/>'
    f'<rect x="25.5" y="58" width="9" height="1.8" fill="{FACE}"/>'
    f'<ellipse cx="42" cy="26" rx="9" ry="3.5" transform="rotate(-35 42 26)" fill="{GREEN}"/>'
    f'<ellipse cx="18" cy="52" rx="9" ry="3.5" transform="rotate(30 18 52)" fill="{GREEN}"/>'
    f'<circle cx="30" cy="9" r="3.5" fill="{RED}"/>'
)


def tile(x, y, s, edge):
    """The tile at (x, y), scaled by s, with an edge that deep under it."""
    w, h = 60 * s, 80 * s
    rx = w * .09
    return (f'<rect x="{x}" y="{y + edge}" width="{w}" height="{h}" rx="{rx}" fill="{EDGE_DARK}"/>'
            f'<rect x="{x}" y="{y + edge * .75}" width="{w}" height="{h}" rx="{rx}" fill="{EDGE}"/>'
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{FACE}"/>'
            f'<g transform="translate({x} {y}) scale({s})">{BAMBOO_1}</g>')


def favicon_svg():
    """Transparent, the tile the square's full height."""
    s = 58 / 80
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">{tile((64 - 60 * s) / 2, 1, s, 4)}</svg>'


def face_svg():
    """The tile's face alone, 60x80, for warping."""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 60 80">'
            f'<rect width="60" height="80" rx="{60 * .09}" fill="{FACE}"/>{BAMBOO_1}</svg>')


def _coeffs(dst, src):
    """PIL's PERSPECTIVE coefficients taking output quad dst to input quad src."""
    rows, rhs = [], []
    for (x, y), (u, v) in zip(dst, src):
        rows.append([x, y, 1, 0, 0, 0, -u * x, -u * y]); rhs.append(u)
        rows.append([0, 0, 0, x, y, 1, -v * x, -v * y]); rhs.append(v)
    # 8x8 by Gaussian elimination: no numpy needed
    n = 8
    m = [r + [b] for r, b in zip(rows, rhs)]
    for c in range(n):
        piv = max(range(c, n), key=lambda r: abs(m[r][c]))
        m[c], m[piv] = m[piv], m[c]
        for r in range(n):
            if r != c:
                f = m[r][c] / m[c][c]
                m[r] = [a - f * b for a, b in zip(m[r], m[c])]
    return [m[i][n] / m[i][i] for i in range(n)]


# A real tile's proportions, in millimetres: 35 long, 26 wide, 20 thick,
# the face's ivory a little over half the thickness, the back below it.
# It lies on its side, the long edge across, as a tile photographed on a
# table does, so the face's art is turned a quarter, its top to the left.
TILE_W, TILE_H, TILE_D, FACE_D = 35.0, 26.0, 20.0, 15.5
# The camera: in front, above and to the right of the tile, looking down
# at its middle -- the top, the near side and the right end all in view,
# as in the photograph it was matched to -- close enough that every set
# of edges converges: three-point perspective.
CAMERA, FOCAL = (20.0, -50.0, 56.0), 1.0
LIGHT = (-0.45, -0.35, 0.82)     # from above, front left


def _sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def _dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def _cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def _norm(a):
    n = _dot(a, a) ** .5
    return (a[0] / n, a[1] / n, a[2] / n)


def _shade(hexc, k):
    c = hexc.lstrip('#')
    return tuple(min(255, round(int(c[i:i + 2], 16) * k)) for i in (0, 2, 4)) + (255,)


def perspective_png(px, bg=None, fill=.9):
    """The tile in three-point perspective: a block 26x35x20 lying face up,
    seen from above, in front and to the right, its face (warped from
    face_svg) on top and its two-layer sides below, lit from the front
    left. Transparent around it unless bg is given; fill is how much of
    the square it may take."""
    from PIL import ImageDraw
    ss = 4
    big = px * ss
    target = (0.0, 0.0, TILE_D * .5)
    f = _norm(_sub(target, CAMERA))
    r = _norm(_cross(f, (0, 0, 1)))
    u = _cross(r, f)

    def proj(p):
        v = _sub(p, CAMERA)
        z = _dot(v, f)
        return (FOCAL * _dot(v, r) / z, -FOCAL * _dot(v, u) / z)

    # the outline, a rounded rectangle as the face is drawn (rx 9% of the width)
    rad, outline = TILE_W * .09, []
    for cx, cy, a0 in ((TILE_W / 2 - rad, TILE_H / 2 - rad, 0), (-TILE_W / 2 + rad, TILE_H / 2 - rad, 90),
                       (-TILE_W / 2 + rad, -TILE_H / 2 + rad, 180), (TILE_W / 2 - rad, -TILE_H / 2 + rad, 270)):
        for k in range(13):
            a = math.radians(a0 + 90 * k / 12)
            outline.append((cx + rad * math.cos(a), cy + rad * math.sin(a)))
    corners = [(-TILE_W / 2, TILE_H / 2, TILE_D), (TILE_W / 2, TILE_H / 2, TILE_D),
               (TILE_W / 2, -TILE_H / 2, TILE_D), (-TILE_W / 2, -TILE_H / 2, TILE_D)]
    pts = [proj((x, y, z)) for x, y in outline for z in (0, TILE_D)] + [proj(c) for c in corners]
    x0, x1 = min(p[0] for p in pts), max(p[0] for p in pts)
    y0, y1 = min(p[1] for p in pts), max(p[1] for p in pts)
    k = fill * big / max(x1 - x0, y1 - y0)
    ox, oy = (big - (x1 - x0) * k) / 2 - x0 * k, (big - (y1 - y0) * k) / 2 - y0 * k

    def scr(p):
        x, y = proj(p)
        return (ox + x * k, oy + y * k)

    out = Image.new('RGBA', (big, big), (0, 0, 0, 0))     # the background, if any, goes under at the end
    draw = ImageDraw.Draw(out)
    light = _norm(LIGHT)
    n = len(outline)
    for i in range(n):
        (ax, ay), (bx, by) = outline[i], outline[(i + 1) % n]
        nx, ny = by - ay, -(bx - ax)                      # outward, the outline running anticlockwise
        mid = ((ax + bx) / 2, (ay + by) / 2, TILE_D / 2)
        if _dot(_sub(mid, CAMERA), (nx, ny, 0)) >= 0:     # facing away
            continue
        lit = .78 + .22 * max(0.0, _dot(_norm((nx, ny, 0)), light))
        # the face's ivory down the sides, then the back's darker layer
        for z0, z1, col in ((0, TILE_D - FACE_D, DARK_BG), (TILE_D - FACE_D, TILE_D, FACE)):
            draw.polygon([scr((ax, ay, z0)), scr((bx, by, z0)), scr((bx, by, z1)), scr((ax, ay, z1))],
                         fill=_shade(col, lit))
    # the face: the plane's projection is exactly a homography of its square
    fw, fh = 600, 800
    face = Image.open(io.BytesIO(cairosvg.svg2png(bytestring=face_svg().encode(), output_width=fw, output_height=fh))).convert('RGBA')
    face = face.rotate(90, expand=True)     # a quarter anticlockwise: the art's top to the left
    fw, fh = face.size
    quad = [scr(c) for c in corners]
    warped = face.transform((big, big), Image.PERSPECTIVE, _coeffs(quad, [(0, 0), (fw, 0), (fw, fh), (0, fh)]), Image.BICUBIC)
    out.alpha_composite(warped)
    # an outline round the whole tile in the same dark, about 1.2% of the icon
    from PIL import ImageFilter
    ow = max(3, round(big * .012)) | 1
    ring = out.getchannel('A').filter(ImageFilter.MaxFilter(2 * ow + 1))
    # and round that, a white one as wide, so the dark edge shows on a dark screen too
    halo = ring.filter(ImageFilter.MaxFilter(2 * ow + 1))
    under = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    under.paste(Image.new('RGBA', (big, big), '#ffffff'), (0, 0), halo)
    under.paste(Image.new('RGBA', (big, big), DARK_BG), (0, 0), ring)
    under.alpha_composite(out)
    out = under
    if bg:
        out = Image.alpha_composite(Image.new('RGBA', (big, big), bg), out)
    out = out.resize((px, px), Image.LANCZOS)
    buf = io.BytesIO()
    out.save(buf, 'PNG', optimize=True)
    return buf.getvalue()


QR_PX, QRICON_PX = 160, 200     # the web's (2x its largest showing), the Tab5's (as large as portrait draws it)


def qricon_c(root):
    """m5tab5/main/qricon.[ch]: the tile on white, RGB565, row by row."""
    img = Image.open(io.BytesIO(perspective_png(QRICON_PX, bg='#ffffff', fill=1.0))).convert('RGB')
    raw = img.tobytes()
    px = [((raw[i] >> 3) << 11) | ((raw[i + 1] >> 2) << 5) | (raw[i + 2] >> 3) for i in range(0, len(raw), 3)]
    head = ('/*\n * qricon -- the app\'s icon, the 1 Bamboo tile in perspective, on white:\n'
            ' * the middle of the replay QR code (ui.c, draw_share).\n *\n'
            ' * GENERATED FILE. Do not edit. Regenerate with:\n *     python3 tools/icons.py\n *\n'
            ' * SPDX-License-Identifier: MIT\n */\n')
    with open(os.path.join(root, 'm5tab5', 'main', 'qricon.h'), 'w') as f:
        f.write(head + '#pragma once\n#include <stdint.h>\n\n'
                f'#define QRICON_PX ({QRICON_PX})\n\n'
                '/* QRICON_PX x QRICON_PX pixels, RGB565, opaque */\n'
                'extern const uint16_t qricon[QRICON_PX * QRICON_PX];\n')
    rows = [', '.join(f'0x{v:04x}' for v in px[i:i + 12]) for i in range(0, len(px), 12)]
    with open(os.path.join(root, 'm5tab5', 'main', 'qricon.c'), 'w') as f:
        f.write(head + '#include "qricon.h"\n\nconst uint16_t qricon[QRICON_PX * QRICON_PX] = {\n    '
                + ',\n    '.join(rows) + ',\n};\n')
    print('wrote m5tab5/main/qricon.[ch]')


def png(svg, px):
    return cairosvg.svg2png(bytestring=svg.encode(), output_width=px, output_height=px)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.makedirs(os.path.join(root, 'web', 'icons'), exist_ok=True)

    # apple-touch-icon.png: 80% of the width keeps the tile clear of the
    # corners iOS rounds (a radius of about 22%)
    # a maskable icon keeps its art in the circle of radius 40% at the
    # centre, and fills the square: the tile, its outlines and all, at
    # 62% of the width, has its farthest corner just inside that circle
    out = {
        'apple-touch-icon.png': perspective_png(180, bg=SURFACE, fill=.8),
        'icons/icon-192.png': perspective_png(192),
        'icons/icon-512.png': perspective_png(512),
        'icons/maskable-512.png': perspective_png(512, bg=SURFACE, fill=.62),
    }
    for name, data in out.items():
        with open(os.path.join(root, 'web', name), 'wb') as f:
            f.write(data)
        print(f'wrote {name}')

    fav = favicon_svg()
    # %23 for #: a bare # would end the data URI at the first colour
    svg_uri = 'data:image/svg+xml,' + urllib.parse.quote(fav.replace('"', "'"), safe=" =:/'<>(),.-")
    png_uri = 'data:image/png;base64,' + base64.b64encode(png(fav, 32)).decode()
    page = os.path.join(root, 'web', 'index.html')
    with open(page, encoding='utf-8') as f:
        html = f.read()
    html, n1 = re.subn(r'(<link rel="icon" type="image/png" sizes="32x32" href=")[^"]*(">)',
                       lambda m: m.group(1) + png_uri + m.group(2), html)
    html, n2 = re.subn(r'(<link rel="icon" type="image/svg\+xml" sizes="any" href=")[^"]*(">)',
                       lambda m: m.group(1) + svg_uri + m.group(2), html)
    if n1 != 1 or n2 != 1:
        sys.exit("index.html: expected one PNG and one SVG favicon <link> to rewrite")
    qr_uri = 'data:image/png;base64,' + base64.b64encode(perspective_png(QR_PX, fill=1.0)).decode()
    html, n3 = re.subn(r"(const QR_ICON = ')[^']*(';)", lambda m: m.group(1) + qr_uri + m.group(2), html)
    if n3 != 1:
        sys.exit("index.html: expected one `const QR_ICON = '...';` to rewrite")
    with open(page, 'w', encoding='utf-8') as f:
        f.write(html)
    print('rewrote the favicons and QR_ICON in index.html')
    qricon_c(root)


if __name__ == '__main__':
    main()
