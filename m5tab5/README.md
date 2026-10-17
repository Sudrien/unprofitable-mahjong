# Unprofitable Mahjong for the M5Stack Tab5

The same game as `../index.html`, as firmware for the M5Stack Tab5
(ESP32-P4, 5″ 1280×720 touch panel), played in landscape.

**Status: not built yet.** This folder is being filled in a patch at a
time. Nothing here has run on a board.

## Plan

| Part | What | Built on |
|---|---|---|
| `core/` | Rules: deal, sets and scores, melds, discards, penalty, hint. Plain C, no IDF. | -- |
| `test/` | Host tests that check `core/` against the web version's own code, run with Node. | gcc, Node |
| `tools/` | Renders the 34 tile faces from the web version's drawings into RGB565 arrays, committed like Ark12. | Python, Pillow |
| `main/` | Board bring-up, drawing and touch. | `feckless_graphics_handler`, `feckless_drivers` |

Drawing goes through `gfx`: the shadow buffer is redrawn where something
changed and blitted a band at a time, so tapping a tile costs one row of
tiles, not a frame. Text is Ark12 at scale; it already carries the CJK
the Japanese and Chinese hand names need, except 雀.

Saves (the game in progress, best scores, language) go in NVS. The SD
card is not needed for anything yet, so `feckless_storage_handler` is not
a dependency until something is.

## Testing the rules

`core/` is checked against the web version itself. `test/check.js` loads
the rules straight out of `../index.html` and compares them with the C
build, move by move:

    make -C m5tab5/test              # 2000 random games, about a minute
    make -C m5tab5/test GAMES=200    # a quick run

It compares about 57,000 tile groups (set and points) and every move of
the games: the table, melds, wall, discard pile, points, penalty, the
hint's choice and the stranded-honor warning. It needs a C compiler and
Node.js, and builds with AddressSanitizer and UBSan.

## Credits

Same as the web version: designed by Sudrien, written with Claude.
