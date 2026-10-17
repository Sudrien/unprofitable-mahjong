# Unprofitable Mahjong for the M5Stack Tab5

The same game as `../index.html`, as firmware for the M5Stack Tab5
(ESP32-P4, 5″ 1280×720 touch panel), played in landscape.

**Status: written, not yet built with ESP-IDF or run on a board.** The
rules are tested against the web version, and the screen code has been
run on a PC through the graphics library's own host shim (below), but
nobody has flashed it yet. English only for now.

![The table](screenshots/table.png)

## Building

    cd m5tab5
    idf.py set-target esp32p4
    idf.py build flash monitor

ESP-IDF 5.4.2 or later. The libraries are fetched by the component
manager at the tags in `main/idf_component.yml`; the first build writes
`dependencies.lock`, which should be read before it is committed.

## Plan

| Part | What | Built on |
|---|---|---|
| `core/` | Rules: deal, sets and scores, melds, discards, penalty, hint. Plain C, no IDF. | -- |
| `test/` | Host tests that check `core/` against the web version's own code, run with Node. | gcc, Node |
| `tools/` | Renders the 34 tile faces from the web version's drawings into RGB565 arrays, committed like Ark12. | Python, Pillow |
| `main/` | Board bring-up, drawing, touch and saves. | `feckless_graphics_handler`, `feckless_drivers` |
| `test/screen/` | Runs `main/ui.c` on a PC with scripted taps and saves what it draws. | gcc, the graphics library's host shim |

Drawing goes through `gfx`: the shadow buffer is redrawn where something
changed and blitted a band at a time, so tapping a tile costs one row of
tiles, not a frame. Text is Ark12 at scale; it already carries the CJK
the Japanese and Chinese hand names need, except 雀.

Saves (the game in progress, best scores, settings) go in NVS after
every move, so pulling the power loses nothing. The SD card is not
needed for anything yet, so `feckless_storage_handler` is not a
dependency until something is.

## The screen

Landscape, 1280×720, in gfx's shadow buffer: the scores along the top,
the rail of recent clears, the meld row, the hand (two rows of 9, or 12
after Add 6), a status line and nine buttons. Tap tiles to select them;
tap a meld to select all of it. Undo goes back up to 64 moves, and like
the web version it cannot reach past Add 6 or Discard 6.

![Two dragon pungs melded, the third on the table](screenshots/melds.png)

Clearing a limit hand plays the celebration: the tiles land one at a
time, the name, and the points counting up.

![Big Three Dragons](screenshots/limit-hand.png)

## Seeing the screen without a board

`test/screen/` compiles `main/ui.c`, the tile drawing and the rules
exactly as they ship, with feckless-graphics-handler's real `gfx.c`
through that library's own host shim. Touch is replaced by a script of
taps, and the screen is saved before each one:

    make -C m5tab5/test/screen GFX=/path/to/feckless-graphics-handler-for-m5tab5
    cd m5tab5/test/screen && mkdir -p out
    ./screentest out/shot 660,660 310,290 640,435 75,660

`SCREENTEST_DEAL=dragons` starts from three dragon pungs, for the meld
row and the celebration. The screenshots above came from it.

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
