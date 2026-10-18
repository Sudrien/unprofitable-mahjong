# Unprofitable Mahjong

A slow, single-player mahjong for a small high-resolution screen. It was designed for the M5Stack Tab5 (5″ 1280×720) and runs in any browser as one self-contained `index.html`.

Work through all 136 tiles, a window of 18 at a time, clearing them in sets until none are left. Points come from the sets you make. Penalty counts every tile you haven't cleared yet and every shortcut you take. The two are kept separate: a big score and a clean finish pull against each other.

Available in English, 日本語 and 简体中文. The default follows the browser language, and there's a switch in the header.

## Play

Open [index.html](https://sudrien.github.io/unprofitable-mahjong/) in a browser. There's nothing to install or build. Your game, best scores, language and settings are saved in the browser.

After one visit online it plays offline, fonts included. It can also be installed as an app: *Install* in Chrome or Edge's address bar, or *Share → Add to Home Screen* on iPhone and iPad. Online, it always loads the newest version.

`index.html` also runs on its own, opened straight from disk; it just won't play offline or install that way. The offline copy is kept by `sw.js`, and `manifest.webmanifest` makes it installable. The icons are drawn by `tools/icons.py`.

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
  - **Discard set** (optional, off by default; turn on *Discard sets*): a set from the table goes to the discard pile instead, for what it would have scored. Six tiles always go as Discard 6.
  - Adding and discarding can't be undone.
- **Every deal is solvable with no penalty.** Deals are built from a full plan of sets, then shuffled while checking that the plan still works.

The in-game **Sets and scores** guide shows every set with example tiles and its points. Tap a limit-hand card to preview its animation.

**Firsts:** the first time you clear each kind of set (40 in all, each run length counting on its own), it plays the limit-hand celebration, marked *First time* with the game number. Games are counted on the device, one per deal. Each set celebrates once per device. The guide ticks the sets you've cleared, notes the game each was first cleared in, and counts how many you have. Replays never count, and don't add to the game count.

**More achievements**, each also celebrated once per device:

- **Full** (★ in the guide): clear a set in 3 of any 5 games in a row. Each set, and each run length, has its own.
- **Win with no poker hands:** clear all 136 without two pair, a flush, a straight, a full house or a royal flush.
- **Win with no pairs:** clear all 136 with no pair in any set. Pairs, two pair and full houses all count against it, and so do hands with a pair inside them, like All Honors, Seven Pairs, Thirteen Orphans and Nine Gates.

When two come at once, such as the last set's celebration and a win, they play one after the other.

## Replay codes

**Share**, by New deal, turns the game on the table into a link anyone can open to watch it move by move. On the published site the link is the short `sudrien.github.io/umj/#replay=…`, which forwards here from its own repository. The code is the deal's seed and the moves, about 3 characters a move (around 150 for a whole game), in URL-safe base64. Nothing in it states a score: the page replays every move under the rules and stops at the first one that breaks them, so the only way to show a score is a legal game that earns it. Making a code also takes a small proof of work (MurmurHash2 with 20 leading zero bits, under a second), and every 63 characters carry 2 check characters that put one mistyped character right. Watching a code never touches your own game or best scores. Games dealt before codes existed have none. On the Tab5, Share in the settings shows the same link as a QR code.

`node test/replaycheck.js` plays random games with every kind of move, and checks that each code replays to the same end, that a typo is put right, and that changed codes are refused.

## Simulations

The bots in `sim/` play the game with its own code; see [sim/README.md](sim/README.md).

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
| [QR Code generator library](https://github.com/nayuki/QR-Code-generator) (C), © Project Nayuki | The Tab5's replay QR code, vendored in `m5tab5/components/qrcodegen` | MIT |

The firmware binaries on the releases page contain the Tab5 entries in this table, so anyone redistributing them should carry those licences too. Each project's repository has its licence text.
