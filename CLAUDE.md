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

**The `umode` branch has a numbering of its own: `u-unprofit-NNNN`.**
Its patches are named `u-unprofit-0001-<subject>.patch`, `u-unprofit-0002-...`,
and apply to the `umode` branch, not to main. They are numbered from
`--start-number 1` and renamed with the prefix; the subject has neither.
Their entries are under "The `umode` branch" at the end of this file,
headed `u-unprofit-NNNN`; look at the last one for the next number. The
branch pins feckless-libs' own `umode` branch (`u-feckless-NNNN`), not a
tag, and needs ESP-IDF 6.1; Avoidant's `umode` branch (`u-avoidant-NNNN`)
is the launcher that runs it. Where that section and the rules under
`m5tab5/` below disagree, on this branch that section is right. When the
branch merges, its entries move into this file's own sections.

## m5tab5/

Built with ESP-IDF (>= 5.4.2) on the feckless libraries from
[feckless-libs-for-m5tab5](https://github.com/Sudrien/feckless-libs-for-m5tab5),
all named at one tag in `main/idf_component.yml`:
`feckless_graphics_handler`, `feckless_drivers`, `feckless_camera`,
`feckless_nvs_handler`, `feckless_abi` and its `feckless_abi_nvs` and
`feckless_abi_camera` sections. The libraries came out
of [defeatist-music-player-for-m5tab5](https://github.com/Sudrien/defeatist-music-player-for-m5tab5);
its CLAUDE.md is the longer version of the
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
- **The game is an ELF built against the feckless ABI** (`elf/`),
  with NVS and the camera from its nvs and camera sections
  (`feckless_abi_nvs`, `feckless_abi_camera`). `main/standalone.c` is
  the .bin's app_main and only loads it; the published `.elf` runs on
  a launcher (Avoidant) as it is. A call from the game to anything
  outside the ABI fails the build: link it into the ELF
  (`ELF_COMPONENTS` in `elf/CMakeLists.txt`, as the rules, qrcodegen
  and quirc are), or it is a patch against feckless-libs first. The
  shape is Lothesome Audio Analyzer's.
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

- **Overcompensating** (secret, both sides): a limit hand cleared with
  four of one tile, where three would do -- any limit hand but Four
  Kongs and Seven Pairs. Counted by tiles, so All Green's 2222 split
  into chows and a pair earns it too.

- **Scan a code (Tab5):** in the settings beside Share. The camera's
  view (640x360, every second pixel), read by quirc through qrscan.c;
  um_replay_read takes the code as far as its seed. A deal (no moves)
  is offered and played as a loaded game (UM_HAD_LOADED, Firsts only)
  unless a limit hand is in its first 18 (um_limit_in_deal).

- **Loading secrets (Tab5):** Guaranteed Random when a scanned deal is
  played, Try Try Again when a scanned deal's seed (no moves, as on
  the web) is in a record,
  Shadows of the Past when a scanned code's game, read move by move by
  um_replay_read_game, was finished.

- **Goals (Tab5):** up to three, kept with the settings (NVS "goals");
  chosen from a set's card in the Sets list (a button beside Done, not
  offered once the set is Full), and marked there; shown beside the
  buttons by name only -- right of the row in landscape, right of the
  two shorter rows in portrait (Add 6 and Hint, Discard and Sort); met
  with a "Goal met" banner.

## Not yet built

- For reference: the brute forced maximum score is 804 & -4; bots
  average under 300 and never reached 500.
- tab5: watching a code with moves on the Tab5 itself -- Scan a code
  reads and checks the whole game (um_replay_read_game) but sends it to
  the website to be watched.

## The `umode` branch

The game built for a box. It runs in RISC-V user mode, in one block of
PSRAM that the PMP grants it and nothing else, and the launcher answers
and checks every call it makes outside that block. The launcher side is
feckless-libs' `umode` branch (read here at dbbb183, `u-feckless-0001` to
`0058`) and Avoidant's (01785a4, `u-avoidant-0001` to `0004`). The game
is the fourth app built for the box, after Spurious, Lothesome and
Defeatist, and what their branches found is what this one starts from.

**What a box does not do for the game**, each from one of them:

- **No camera.** The box's table has no `camera_*` and no
  `feckless_abi_camera_v1`. `main/absent.c` defines the six calls to
  fail as a Tab5 without a camera does (Defeatist's `absent.c` is the
  shape), so Scan a code says "The camera is not available." and nothing
  is scanned: no scanned deals, and Guaranteed Random, Try Try Again and
  Shadows of the Past cannot be earned on this branch. Getting it back
  is a patch against feckless-libs first: the camera answered in a box.
- **`gfx_px` and `gfx_rotation` are not answered** either. Only the
  camera's view calls them; `absent.c` has both.
- **NVS only in the namespace the launcher names** (u-feckless-0053),
  and Avoidant names the file's name up to its first `_` or `.`
  (u-avoidant-0004): `unprofitable`, for `unprofitable_mahjong...elf`. So
  save.c's namespace is `unprofitable`, no longer `umj`. Under any other
  name every read finds nothing and every write is refused, quietly. A
  file renamed so that it starts otherwise saves nothing, or saves beside
  another app's. Blobs are 8 KB at most; the game's are 3 KB and 1.5 KB.
- **`nvsh_migrate` is not answered**, so the move from `umj` is
  standalone.c's, outside the box. A game saved by the trusted ELF under
  Avoidant's main branch stays in `umj`, where a box cannot reach it.
- **A window holds interrupts off on its core until the app's next host
  call** (u-feckless-0017). On core 0 that starved the panel (Spurious's
  blue, Lothesome's cyan), so a launcher brings the board up on core 0
  and runs the app on core 1 (u-feckless-0031); standalone.c does the
  same. A long computation still needs a host call now and then
  (u-spurious-0002): see u-unprofit-0002.
- **Every host call is a trap**, a few microseconds; `gfx_fb()`,
  `gfx_w()` and `gfx_h()` about one (u-feckless-0030). tiledraw.c asks
  for the three once a tile, not once a pixel, which is left as it is.
- **No `rand`, no `open`/`read`, no threads beyond eight.** The game
  uses none of them.
- **A boxed ELF runs only in a box.** Avoidant's main branch, and any
  launcher without `feckless_abi_box_v1`, refuses it by that name; this
  branch's `.elf` is for Avoidant's `umode` branch.

### u-unprofit-0001 -- the game in a box

- `elf/CMakeLists.txt`: `feckless_abi_box_elf()` replaces
  `feckless_abi_elf()`. The ELF links in `feckless_box_libc` -- the pure
  part of libc, the float and double maths quirc and qrcodegen use, the
  log calls' formatting, `heap_caps_malloc` -- and the build fails if it
  imports any of that, or a name the box does not answer. The rules,
  qrcodegen and quirc are linked in as before.
- `main.c`: `feckless_abi_require_box()`; `feckless_abi_require_camera()`
  is gone, since a box does not answer that symbol.
- `main/absent.c`, new: the camera's six calls, `gfx_px` and
  `gfx_rotation`, as above. The ELF's `main` no longer requires
  `feckless_abi_camera`; it keeps `feckless_camera` for `camera.h`.
- `save.c`: the namespace `unprofitable`.
- `standalone.c`: the .bin runs its embedded ELF in a box, not through
  `feckless_abi_run()`, which would refuse it. It moves `umj`'s ten keys
  into `unprofitable` (`nvsh_migrate`), names that namespace for the
  box, brings up the board on core 0, and runs the game on core 1 in the
  largest PSRAM block less 3 MB, at most 16 MB, with a 64 KB stack. Its
  component still requires `feckless_abi_nvs` and `feckless_abi_camera`,
  unused now: the .bin's configuration is left as main's.
- `sdkconfig.defaults`: `CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE=n`,
  as the other umode branches have it. **`rm sdkconfig`.**
- `main/idf_component.yml`: the nine feckless pins move from `v0.8.7`
  to `umode`; `feckless_box_libc`, `feckless_umode`,
  `feckless_box_calls`, and the `feckless_storage_handler`, `fatfs` and
  `usb_host_msc` that the last requires by name (u-lothesome-0002) are
  added. The IDF floor moves from 5.4.2 to 6.1. **`dependencies.lock`
  is not updated**: the next build re-resolves it.

The ELF's imports were read from the sources, not from a link: every
identifier in `main/`, `core/`, qrcodegen and quirc that is one of the
ABI's names (82 of them) was looked up in feckless-libs' `inapp.txt` and
`boxcalls_names.txt`. Eight were in neither -- the camera's six,
`gfx_px`, `gfx_rotation` -- and are absent.c's. `esp_app_get_description`
is linked into the ELF by `feckless_abi_elf()`. A name this missed fails
the ELF's build (abicheck or boxcheck), by name.

*Checked:* absent.c and standalone.c compile against feckless-libs'
umode headers on a host, with stand-ins for IDF's. *Not checked:* an
ESP-IDF build of the ELF or the .bin -- the move to IDF 6.1, with
`components/esp_ipa`'s prebuilt library, and from v0.8.7's ABI to the
branch's (picolibc) come with this patch; a board; `m5tab5/test/`, which this does not touch (save.c's
namespace is a string to savecheck's fake NVS).

*What to look for on a Tab5*, standalone or under Avoidant's umode
branch:
- in the ELF's build: `feckless_box_libc: boxed, N imports`;
- standalone: `N saved values moved from "umj"` once, on a Tab5 that had
  a game; under Avoidant: `...: NVS in "unprofitable"`;
- `feckless_umode: app in its box`, then `=== Unprofitable Mahjong ===`;
- `no camera -- seeds cannot be read from QR codes`, always;
- a deal drawn, a move made, the Tab5 restarted, the game still there;
- Share: the QR code, and no cyan while "Working..." shows;
- Scan a code: "The camera is not available."

Neither the standalone .bin nor Avoidant gives a way out of the game
but the power; the game never had an exit.
