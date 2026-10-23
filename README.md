# Unprofitable Mahjong

A slow, single-player mahjong for a small high-resolution screen. It was designed for the M5Stack Tab5 (5″ 1280×720), runs there as firmware (`m5tab5/`), and runs in any browser as one self-contained `web/index.html`.

Work through all 136 tiles, a window of 18 at a time, clearing them in sets until none are left. Points come from the sets you make. Penalty counts every tile you haven't cleared yet and every shortcut you take. The two are kept separate: a big score and a clean finish pull against each other.

Available in English, 日本語 and 简体中文. The default follows the browser language, and there's a switch in the header.

## Play

Open [index.html](https://sudrien.github.io/unprofitable-mahjong/) in a browser. There's nothing to install or build. Your game, best scores, language and settings are saved in the browser.

After one visit online it plays offline, fonts included. It can also be installed as an app: *Install* in Chrome or Edge's address bar, or *Share → Add to Home Screen* on iPhone and iPad. Online, it always loads the newest version.

The footer names the build you're playing: the commit it was published from, linked to its code. `.github/workflows/pages.yml` publishes the site and writes that commit in, so Pages is set to deploy from GitHub Actions; anywhere else the footer says *local*.

`web/index.html` also runs on its own, opened straight from disk; it just won't play offline or install that way. The offline copy is kept by `web/sw.js`, and `web/manifest.webmanifest` makes it installable. The icons are drawn by `tools/icons.py`.

## Rules

- **The table:** 18 tiles face up. Cleared spaces refill from the wall.
- **Sets:** select tiles and clear them as a set. The basic sets are pair, chow (three in a row, one suit), pung (three of a kind) and kong (four of a kind). Pungs and kongs of 1s, 9s, winds and dragons score double.
- **Bigger sets** score more: runs of 4–8, full straight, double and triple chows, dragon and wind sets, and classic hands. The classic hands include Little Three Dragons, Triple Pung, Mixed Straight, Three Kongs, All Simples, Outside Hand, Half Flush, All Pungs and Full Flush.
- **Limit hands** play a short animation: Thirteen Orphans, Nine Gates, Big and Little Four Winds, All Honors, All Terminals, Big Three Dragons, Seven Pairs, All Green and Four Kongs. In these hands a kong counts as a pung.
- **Poker hands** are allowed but score little: two pair, straight, flush, full house, royal flush. If their tiles also split into mahjong sets, they score those sets plus 1.
- **Melds:** set aside up to 4 pairs, chows, pungs, kongs, runs or straights. Melds score nothing until cleared and don't count toward the table limit. A melded pung can become a kong; it is the only meld that can grow, so a melded pair, chow, run or straight stays as it is. Select melds together with table tiles to clear one bigger hand.
- **Penalty:** starts at −272 (2 per tile) and is reduced by 2 for each tile you clear. A clean sweep ends at 0.
  - **Add 6** (−6): six more tiles on the table, up to 24 out.
  - **Discard 6** (−6): six tiles go to the discard pile. Discards come back, in order, once the wall is empty.
  - **Discard set** (optional, off by default; turn on *Discard sets*): a set from the table goes to the discard pile instead, for what it would have scored. Six tiles always go as Discard 6.
  - Adding and discarding can't be undone.
- **Every deal is solvable with no penalty.** Deals are built from a full plan of sets, then shuffled while checking that the plan still works.

The in-game **Sets and scores** guide shows every set with example tiles and its points. Tap a limit-hand card to preview its animation.

**Firsts:** the first time you clear each kind of set (40 in all, each run length counting on its own), a banner slides in at the top for a few seconds, marked *First time* with the game number; it never stops play. A limit hand's First plays its full celebration instead, marked *First limit hand*. Games are counted on the device, one per deal. Each set celebrates once per device. The guide ticks the sets you've cleared, notes the game each was first cleared in, and counts how many you have. Replays never count, and don't add to the game count.

**More achievements**, each earned once per device. Full shows the same banner; the wins and the flood play the full celebration:

- **Full** (★ in the guide): clear a set in 3 of any 5 games in a row, at least 3 of them different deals, and only games you complete count: finished, or stuck with nothing left to clear, not abandoned for a new deal. A clear that would make a set Full says *finish the game to confirm*, and the Full comes when the game ends. Each set, and each run length, has its own.
- **Win with no poker hands:** clear all 136 without two pair, a flush, a straight, a full house or a royal flush.
- **Win with no pairs:** clear all 136 with no pair in any set. Pairs, two pair and full houses all count against it, and so do hands with a pair inside them, like All Honors, Seven Pairs, Thirteen Orphans and Nine Gates.
- **Win with every poker hand:** clear all 136 with two pair, a flush, a straight and a full house among your sets. A royal flush counts as the flush.

- **The Yellow River Floods Again:** clear every set at least once, all 40 Firsts. Its card at the top of the guide fills like a rising river as you go.
- **The Capital is Gone:** make every set Full. Its river fills with each Full.
- **High Score:** 500 points in one game. It isn't a hand, so it's no part of either river.

**Goals:** any set's card in the guide has *Make this a goal*, for up to three at once, until the set is Full. In the browser they sit above the table, each with how far its First and Full have come; on the Tab5, beside the buttons, by name, and the Sets list marks their cards. The next time you clear one of those sets a banner says *Goal met* and that goal clears. A run's card means any run. Kept on the device.

There are also some **secret** achievements. They don't appear in the guide until you find them, and they're no part of either river.

When two full celebrations come at once, such as a limit hand and a win, they play one after the other. Each achievement records the game it was earned in and that game's seed. The guide shows the game number; the seed is kept in the browser, not shown.

## Replay codes

**Share**, by New deal, turns the game on the table into a link anyone can open to watch it move by move. On the published site the link is the short `sudrien.github.io/umj/#replay=…`, which forwards here from its own repository. The code is the deal's seed and the moves, about 3 characters a move (around 150 for a whole game), in URL-safe base64. Nothing in it states a score: the page replays every move under the rules and stops at the first one that breaks them, so the only way to show a score is a legal game that earns it. Making a code also takes a small proof of work (MurmurHash2 with 20 leading zero bits, under a second), and every 63 characters carry 2 check characters that put one mistyped character right. Share shows the link as a QR code too, the same code the Tab5 draws for it, with the app's icon in the middle. Watching a code never touches your own game or best scores. Games dealt before codes existed have none.

**Load**, beside Share, takes a pasted code or link. A code with moves plays back as above. A code shared at the very start of a game, before any move, is just a deal, and you can play it as a new game. A loaded deal earns Firsts, and so fills the river, but not the rest of what play earns: it doesn't count toward Full, and earns no wins or High Score. The table says *Loaded deal · Firsts only* while you play one. Its points still count toward your best scores. Replaying a seed can find Firsts you missed in it before. A deal whose first 18 tiles already hold a limit hand can't be played that way. A replay link with no moves offers the same: **Play this deal** in the replay bar.

On the Tab5, Share in the settings shows the same link as a QR code, and **Scan a code**, beside it, reads one with the Tab5's camera: hold up a phone or another screen showing a replay QR code. A deal can be played there under the same rules; a game with moves is checked move by move, then left to a phone or computer to watch.

`node test/replaycheck.js` plays random games with every kind of move, and checks that each code replays to the same end, that a typo is put right, and that changed codes are refused.

## Simulations

The bots in `sim/` play the game with its own code; see [sim/README.md](sim/README.md).

## On the Tab5

`m5tab5/` is the same game as firmware for the M5Stack Tab5, on ESP-IDF: landscape or portrait, both themes and all three languages, the achievements and goals, Share as a QR code and Scan a code with the camera. Its rules are C, checked move by move against this page. See [m5tab5/README.md](m5tab5/README.md).

## Credits

Designed by Sudrien. Built with [Claude](https://claude.ai) (Anthropic's Claude Opus 5.5), which wrote the game page, the translations and the strategy bots from Sudrien's design, and ran the simulations used to balance the scoring.

## Licence

Unprofitable Mahjong is MIT-licensed; see [LICENSE](LICENSE). That covers the page, the strategy bots, the Tab5 firmware's own code, the tile drawings and the icons. Every source file says so in its header, and a file that carries someone else's work also names it and its licence there.

It uses other people's work under their own licences:

| Work | Where it's used | Licence |
|---|---|---|
| [Figtree](https://fonts.google.com/specimen/Figtree), [Shippori Mincho](https://fonts.google.com/specimen/Shippori+Mincho), [Noto Serif SC](https://fonts.google.com/noto/specimen/Noto+Serif+SC) | Web fonts the page loads from Google Fonts; not stored in this repository | SIL Open Font License 1.1 |
| [Ark Pixel Font](https://github.com/TakWolf/ark-pixel-font) (Ark12), © 2021 TakWolf | The Tab5 firmware's text, compiled in through feckless_graphics_handler | SIL Open Font License 1.1 |
| [feckless-graphics-handler-for-m5tab5](https://github.com/Sudrien/feckless-graphics-handler-for-m5tab5), [feckless-drivers-for-m5tab5](https://github.com/Sudrien/feckless-drivers-for-m5tab5), [feckless-camera-for-m5tab5](https://github.com/Sudrien/feckless-camera-for-m5tab5) | Tab5 firmware, fetched when it's built | MIT |
| [ESP-IDF](https://github.com/espressif/esp-idf) and the Espressif components it pulls in (touch controllers, USB host, the camera sensor's esp_cam_sensor) | Tab5 firmware, fetched when it's built | Apache 2.0 |
| [esp_video](https://github.com/espressif/esp-video-components/tree/master/esp_video) and [esp_ipa](https://github.com/espressif/esp-video-components/tree/master/esp_ipa), © Espressif | The camera: esp_video fetched when it's built; esp_ipa, its image processing, vendored in `m5tab5/components/esp_ipa` (see its VENDORED.md), with a tuning file in `m5tab5/camera/` | Espressif MIT |
| [QR Code generator library](https://github.com/nayuki/QR-Code-generator), © Project Nayuki | The replay QR code: the C version vendored in `m5tab5/components/qrcodegen` for the Tab5, the TypeScript version compiled into `web/index.html` | MIT |
| [quirc](https://github.com/dlbeer/quirc), © Daniel Beer | Reading a replay link's QR code off the Tab5's camera, vendored in `m5tab5/components/quirc` | ISC |
| [Noto Serif CJK](https://github.com/notofonts/noto-cjk), © 2017 Adobe | The Tab5's tile faces (`m5tab5/main/tilefaces.c`): the characters are pictures of its glyphs, rendered by `m5tab5/tools/gen_tiles.py`. The font itself is not included | SIL Open Font License 1.1 |
| [MurmurHash2](https://github.com/aappleby/smhasher), Austin Appleby | The replay codes' proof of work, in `web/index.html` and `m5tab5/core/umreplay.c` | Public domain |

The firmware binaries on the releases page contain the Tab5 entries in this table, so anyone redistributing them should carry those licences too. Each project's repository has its licence text.
