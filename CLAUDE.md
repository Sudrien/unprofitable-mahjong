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
  Wait Wait (Sort, or turning Auto-sort on, threw a selection away),
  and Intentional Game Design (Meld pressed on a melded pair and its
  third tile; Meld still looks disabled, but takes the press).

- **Loading a code (web):** Load takes a pasted code or link. With moves
  it plays back; with none it is a deal, playable as a new game unless a
  limit hand is already among its first 18 tiles. A loaded game is
  marked S.loaded and earns Firsts (so the river) and nothing else: not
  Full, wins or secrets. Replaying a seed can find Firsts it missed.
  Full also needs 3 different deals in its window, on both sides. When
  the Tab5 loads seeds, um_game_t needs the loaded mark too.

- **The Capital is Gone:** every set Full, on both sides. Loaded deals
  never count toward Full, so never toward it.
- **High Score:** 500 points in one game, at the game's end, stuck or
  not; on both sides. Not a hand: no part of either river. Loaded deals
  do not earn it.
- **Play from a link:** a #replay= link with no moves offers Play this
  deal in the replay bar, as Load does.
- **Loading secrets (web):** Guaranteed Random (played a loaded deal),
  Try Try Again (loaded a deal an achievement's record holds), Shadows
  of the Past (opened a finished game). Earned by loading itself, so
  exempt from the loaded-deal rule; recorded at the device's game count
  with the loaded seed.

- **Full on completed games:** Firsts need no finished game (easy to
  hunt); Full counts only games completed, finished or stuck, not given
  up for a new deal. A clear that would make one says "finish the game
  to confirm"; the Full, and the Capital, come at the game's end.
  Loaded deals' points count toward best scores, not High Score.

## Not yet built

- Secret achivements?
  - Overcompensating: used 4 tiles of a kind in a limit hand that requires 3
    (on hold)
- For reference: the brute forced maximum score is 804 & -4; bots
  average under 300 and never reached 500.
- tab5 : qr code reading with camera to enter seeds
