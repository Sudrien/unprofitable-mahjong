# CLAUDE.md

How to work on this repository. Read it before writing a patch.

The web version, `web/index.html`, is the reference implementation of the
rules. The Tab5 firmware in `m5tab5/` follows it: when the two disagree
about a rule or a score, the web version is right until a patch changes
both.

## Patches

Claude creates `git am`-able patches authored as
`Claude <noreply@anthropic.com>`, and presents each one as soon as it
exists -- a session can end with patches stuck behind it.

Claude must not commit to, push to, branch on, or open a pull request
against the repository. The URL a session is given is for pulling only.
Local commits made to run `git format-patch` stay local.

**Claude does not run the tests or the bots unless asked.** That is
`m5tab5/test/`, `test/replaycheck.js` and `sim/`: they take minutes, and
the person decides when that time is spent.

**Patches are cumulative.** Each applies on top of what is already here.
Never hand back a rewritten copy of a file, and never reissue a corrected
version of a patch that has been handed over: send a follow-up with the
next number. A patch already handed over is history.

**A patch has one number, written down once.** The number comes from
`--start-number`, never from the subject:

    git format-patch --start-number 2 -1 -o out/

gives `0002-<subject>.patch`. So the subject is just the subject
(`m5tab5: game rules in C`), never `0002: ...`. Before choosing a number,
look at the last one used (`git log`) and take the next.

## m5tab5/

Built with ESP-IDF (>= 5.4.2) on the same libraries as
[defeatist-music-player-for-m5tab5](https://github.com/Sudrien/defeatist-music-player-for-m5tab5):
`feckless_graphics_handler`, `feckless_drivers` and, where it is needed,
`feckless_storage_handler`. Its CLAUDE.md is the longer version of the
board rules below, learned the hard way; read it before touching memory,
stacks or the build.

- **Every dependency is pinned to a tag.** A moving branch changes
  underneath a build that worked.
- **Any change to an `idf_component.yml` re-resolves `dependencies.lock`**
  on the next build. The Espressif registry is not reachable from a
  session, so a patch that touches a manifest says so in its commit
  message, and the lock diff from the first build is read before it is
  committed.
- **Any change to `sdkconfig.defaults` needs `rm sdkconfig`** before the
  next build; the defaults only fill keys an existing sdkconfig lacks.
  Say so in the commit message.
- **Buffers go in PSRAM** (`MALLOC_CAP_SPIRAM`) unless a peripheral needs
  them internal. Nothing over a few hundred bytes goes on a task stack.
- **A session cannot build the firmware.** There is no ESP-IDF here. What
  a session can do is compile and test the game rules on the host
  (`m5tab5/test/`), which is why the rules are kept free of IDF headers.
  A patch that changes firmware code says it has not been built.

## Rules in C

`m5tab5/core/` is the game with no board in it: dealing, sets, scoring,
melds, the discard pile, penalty. It must give the same answers as the
web version. `m5tab5/test/` checks that against `web/index.html` with Node,
and a patch that changes either side runs it.

## Built

- **Win with every poker hand:** two pair, a flush, a straight and a
  full house among a won game's sets. A royal flush counts as the flush.
- **Secrets**, listed only once earned, and no part of the river:
  A Place for Everything (a game ended with no tiles left) and Wait
  Wait Wait (Sort, or turning Auto-sort on, threw a selection away).

## Not yet built

- **The Capital is Gone:** the Full version of The Yellow River Floods
  Again, earned once every set is Full (cleared in 3 of 5 games in a
  row). If it is ever built, it takes this name. Rare limit hands may
  make it out of reach without loading seeds. If seeds become loadable,
  Full should need 3 different seeds in the window, or replaying one
  seed three times would earn any Full.
- first achivements do not require a game to be completed. author thinks
  this is fine - this means the are easy to hunt. the 'full' achievement
  requires completed games.
- loaded games cannot be played unless they are seed only, no moves. a
  loaded seed is not playable if a limit set is possible from the first
  18 tiles.
- **High score:** an achievement for 500 points in a game (the brute
  forced maximum is 804 & -4; bots average under 300 and never reached
  500). It is not a hand, so it is no part of The Yellow River Floods
  Again or The Capital is Gone.
- Secret achivements?
  - Intentional game design: tried and failed to add to a pair in meld.
    Open: Meld is disabled for a melded pair plus its third tile, so
    there is no press to catch yet.
  - Overcompensating: used 4 tiles of a kind in a limit hand that requires 3
  - Try try again: load a seed already represented in achivements
  - guaranteed random: load a playable seed
  - shadows of the past: load a completed game
- tab5 : qr code reading with camera to enter seeds
