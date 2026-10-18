#!/usr/bin/env python3
"""
icons.py -- draw the page's icons: the 1 Bamboo, as index.html's face(18)
draws it, on its ivory face with the two edge shades under it.

    python3 tools/icons.py          # from the repository root

writes

    apple-touch-icon.png      180x180, opaque: iOS paints transparency black
    icons/icon-192.png        the web app manifest's icons, opaque
    icons/icon-512.png
    icons/maskable-512.png    the same, small enough to survive any mask
                              (the tile inside the central circle)

and rewrites the two favicon <link>s in index.html, which are data URIs so
the page still works opened from disk: an SVG, and a 32x32 PNG for
browsers that take no SVG favicon.

Needs cairosvg and Pillow (pip install cairosvg pillow).

SPDX-License-Identifier: MIT
"""
import base64
import io
import os
import re
import sys
import urllib.parse

import cairosvg

# the light theme's --face, --edge, --edge-dark, --t-green, --t-red and
# --surface, from index.html
FACE, EDGE, EDGE_DARK, GREEN, RED = '#fbf8ef', '#cfc2a3', '#a8987a', '#1e7247', '#b5302a'
SURFACE = '#d3ddd0'

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


def square_svg(tile_h):
    """Opaque, on the table's surface, the tile tile_h of 180 tall, centred."""
    s = tile_h / 80
    edge = 8 * tile_h / 132
    y = (180 - tile_h) / 2 - 6 * tile_h / 132   # a little high, so tile and edge sit centred
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 180 180">'
            f'<rect width="180" height="180" fill="{SURFACE}"/>{tile((180 - 60 * s) / 2, y, s, edge)}</svg>')


def png(svg, px):
    return cairosvg.svg2png(bytestring=svg.encode(), output_width=px, output_height=px)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.makedirs(os.path.join(root, 'icons'), exist_ok=True)

    # 132 of 180: the size apple-touch-icon.png has always been
    plain = square_svg(132)
    # a maskable icon keeps its art in the circle of radius 40% at the
    # centre; the tile's corners, 0.625 of its height from its middle,
    # stay inside at 102 of 180
    maskable = square_svg(102)
    out = {
        'apple-touch-icon.png': png(plain, 180),
        'icons/icon-192.png': png(plain, 192),
        'icons/icon-512.png': png(plain, 512),
        'icons/maskable-512.png': png(maskable, 512),
    }
    for name, data in out.items():
        with open(os.path.join(root, name), 'wb') as f:
            f.write(data)
        print(f'wrote {name}')

    fav = favicon_svg()
    # %23 for #: a bare # would end the data URI at the first colour
    svg_uri = 'data:image/svg+xml,' + urllib.parse.quote(fav.replace('"', "'"), safe=" =:/'<>(),.-")
    png_uri = 'data:image/png;base64,' + base64.b64encode(png(fav, 32)).decode()
    page = os.path.join(root, 'index.html')
    with open(page, encoding='utf-8') as f:
        html = f.read()
    html, n1 = re.subn(r'(<link rel="icon" type="image/png" sizes="32x32" href=")[^"]*(">)',
                       lambda m: m.group(1) + png_uri + m.group(2), html)
    html, n2 = re.subn(r'(<link rel="icon" type="image/svg\+xml" sizes="any" href=")[^"]*(">)',
                       lambda m: m.group(1) + svg_uri + m.group(2), html)
    if n1 != 1 or n2 != 1:
        sys.exit("index.html: expected one PNG and one SVG favicon <link> to rewrite")
    with open(page, 'w', encoding='utf-8') as f:
        f.write(html)
    print('rewrote the favicons in index.html')


if __name__ == '__main__':
    main()
