# Unprofitable Mahjong

A slow, single-player mahjong for a small high-resolution screen. It was designed for the M5Stack Tab5 (5″ 1280×720) and runs in any browser as one self-contained `index.html`.

Work through all 136 tiles, a window of 18 at a time, clearing them in sets until none are left. Points come from the sets you make. Penalty counts every tile you haven't cleared yet and every shortcut you take. The two are kept separate: a big score and a clean finish pull against each other.

Available in English, 日本語 and 简体中文. The default follows the browser language, and there's a switch in the header.

## Play

Open [index.html](https://sudrien.github.io/unprofitable-mahjong/) in a browser. There's nothing to install or build, and it works offline once loaded, apart from the web fonts. Your game, best scores, language and settings are saved in the browser.

## Rules

- **The table:** 18 tiles face up. Cleared spaces refill from the wall.
- **Sets:** select tiles and clear them as a set. The basic sets are pair, chow (three in a row, one suit), pung (three of a kind) and kong (four of a kind). Pungs and kongs of 1s, 9s, winds and dragons score double.
- **Bigger sets** score more: runs of 4–8, full straight, double and triple chows, dragon and wind sets, and classic hands. The classic hands include Little Three Dragons, Triple Pung, Mixed Straight, Three Kongs, All Simples, Outside Hand, Half Flush, All Pungs and Full Flush.
- **Limit hands** play a short animation: Thirteen Orphans, Nine Gates, Big and Little Four Winds, All Honors, All Terminals, Big Three Dragons, Seven Pairs, All Green and Four Kongs. In these hands a kong counts as a pung.
- **Poker hands** are allowed but score little: two pair, straight, flush, full house, royal flush. If their tiles also split into mahjong sets, they score those sets plus 1.
- **Melds:** set aside up to 4 pairs, chows, pungs, kongs, runs or straights. Melds score nothing until cleared and don't count toward the table limit. A melded pung can become a kong. Otherwise melds are fixed. Select melds together with table tiles to clear one bigger hand.
- **Penalty:** starts at −272 (2 per tile) and rises as you clear. A clean sweep ends at 0.
  - **Add 6** (−6): six more tiles on the table, up to 24 out.
  - **Discard 6** (−6): six tiles go to the discard pile. Discards come back, in order, once the wall is empty.
  - Adding and discarding can't be undone.
- **Every deal is solvable with no penalty.** Deals are built from a full plan of sets, then shuffled while checking that the plan still works.

The in-game **Sets and scores** guide shows every set with example tiles and its points. Tap a limit-hand card to preview its animation.

## Strategy notes

These come from the bots in `sim/`, which play using the game's own code:

| Strategy | Points | Penalty |
|---|---|---|
| Follow Hint every turn | ~200 | ~−11, never clears every tile |
| Careful (only clear sets that leave everything groupable) | ~200 | ~0, clears every tile |
| Careful with melds | ~223 | ~−2 |
| Chase big hands, never Add 6 | ~260 | ~−8 |
| Chase big hands using Add 6 | ~290 | ~−65 |
| Bank winds in the discard pile, 6 at a time | ~273 | ~−27, with a wind limit hand in almost every game |

Banking works because discards return in order: winds discarded together come back together, ready for Big or Little Four Winds.

## Simulations

Requires Node.js. Each bot loads the game logic straight from `index.html`.

```sh
node sim/hint.js index.html 5                              # follow Hint
node sim/careful.js index.html 50                          # careful, no leftovers
node sim/strategies.js index.html 50 8 melds summary noadd # big hands, never add
node sim/strategies.js index.html 5 8 melds rows windbank  # bank winds, one row per game
```

`strategies.js` modes are `big`, `noadd`, `hybrid<wall>` (careful once the wall drops below `<wall>` tiles), `carefulmelds`, `stash<n>`, `windbank` and `honorbank`. The third argument is the smallest set value the bot waits for.

## Porting to the Tab5

The layout targets 1280×720 landscape: two rows of 9 tiles, about 10 mm wide each at the Tab5's pixel density, with the meld row above and controls below. On the device, the plan is LVGL on ESP-IDF: tile faces pre-rendered to RGB565 bitmaps, and redraws limited to the tiles that change.

## Credits

Designed by Sudrien. Built with [Claude](https://claude.ai) (Anthropic's Claude Opus 5.5), which wrote the game page, the translations and the strategy bots from Sudrien's design, and ran the simulations used to balance the scoring.

## Licence

Unprofitable Mahjong is MIT-licensed; see [LICENSE](LICENSE). That covers the page, the strategy bots, the Tab5 firmware's own code, the tile drawings and the icons.

It uses other people's work under their own licences:

| Work | Where it's used | Licence |
|---|---|---|
| [Figtree](https://fonts.google.com/specimen/Figtree), [Shippori Mincho](https://fonts.google.com/specimen/Shippori+Mincho), [Noto Serif SC](https://fonts.google.com/noto/specimen/Noto+Serif+SC) | Web fonts the page loads from Google Fonts; not stored in this repository | SIL Open Font License 1.1 |
| [Ark Pixel Font](https://github.com/TakWolf/ark-pixel-font) (Ark12), © 2021 TakWolf | The Tab5 firmware's text, compiled in through feckless_graphics_handler | SIL Open Font License 1.1 |
| [feckless-graphics-handler-for-m5tab5](https://github.com/Sudrien/feckless-graphics-handler-for-m5tab5), [feckless-drivers-for-m5tab5](https://github.com/Sudrien/feckless-drivers-for-m5tab5) | Tab5 firmware, fetched when it's built | MIT |
| [ESP-IDF](https://github.com/espressif/esp-idf) and the Espressif components it pulls in (touch controllers, USB host) | Tab5 firmware, fetched when it's built | Apache 2.0 |

The firmware binaries on the releases page contain the Tab5 entries in this table, so anyone redistributing them should carry those licences too. Each project's repository has its licence text.
