/*
 * umach.h -- the achievements, as the web version keeps them: one struct
 * the board saves whole, and the rules for what a clear or a win earns.
 *
 * The web version (../../web/index.html, "firsts") is the reference; test/
 * plays the same games through both and compares every record.
 *
 *   First   a set cleared for the first time
 *   Full    a set cleared in UM_ACH_FULL_OF of any UM_ACH_WINDOW games in a row,
 *           UM_ACH_FULL_OF different deals among them (a seed played
 *           again is the same deal). Only completed games count --
 *           finished or stuck, not given up for a new deal -- so a clear
 *           that would make one is UM_AWARD_PENDING, and the Full itself
 *           comes from um_ach_end
 *   Wins    all 136 cleared with no poker hand, with no pair in any set,
 *           or with every poker hand (a royal flush is a flush)
 *   The Yellow River Floods Again: every set's First
 *   The Capital is Gone: every set's Full
 *   High score: UM_ACH_HIGH points in one game; not a hand, so no part
 *           of either
 *   Secrets shown only once earned, and no part of the river:
 *           A Place for Everything (a game ended with no tiles left),
 *           Wait Wait Wait (a sort threw a selection away),
 *           Intentional Game Design (Meld pressed to grow a melded pair)
 *
 * The sets are every um_set_t, UM_PAIR to UM_FOUR_KONGS: the 40 the web
 * version counts. Each record is the game it was earned in, by the
 * device's count, and that game's seed. The seed is kept, not shown.
 *
 * No ESP-IDF headers and no allocation.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "umcore.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UM_ACH_WINDOW   (5)     /* FULL_IN */
#define UM_ACH_FULL_OF  (3)     /* FULL_OF */
#define UM_ACH_SETS     (UM_SET_COUNT - 1)   /* the sets there is a First for: 40 */
#define UM_ACH_HIGH     (500)                /* HIGH_POINTS */

typedef struct {
    uint32_t game;      /* the game it was earned in; 0: not earned */
    uint32_t seed;      /* that game's seed */
} um_rec_t;

enum { UM_WIN_NO_POKER, UM_WIN_NO_PAIRS, UM_WIN_ALL_POKER, UM_WIN_N };   /* the web version's WINS, in order */
enum { UM_SECRET_PLACE, UM_SECRET_WAIT, UM_SECRET_DESIGN, UM_SECRET_N }; /* its SECRETS, in order */

typedef struct {
    uint32_t games;                                 /* games dealt on this device */
    um_rec_t first[UM_SET_COUNT];                   /* by um_set_t; [UM_NONE] unused */
    um_rec_t full[UM_SET_COUNT];
    uint32_t played[UM_SET_COUNT][UM_ACH_WINDOW];   /* the games each set was cleared in, the last few; 0 is empty */
    um_rec_t win[UM_WIN_N];
    um_rec_t flood;
    um_rec_t secret[UM_SECRET_N];
    /* Last, so a save from before reads as a prefix: the seed of each
     * game in played[], and which of those seeds are known (bit i for
     * played[t][i]). An entry saved before seeds were kept is unknown,
     * and counts as a deal of its own. */
    uint32_t played_seed[UM_SET_COUNT][UM_ACH_WINDOW];
    uint8_t  played_known[UM_SET_COUNT];
    /* last again, for the same reason */
    um_rec_t capital;
    um_rec_t high;
    /* last again: which of played[t]'s games were completed (bit i for
     * played[t][i]; a save from before has them all completed), and the
     * sets already told "finish the game to confirm" in game pend_game */
    uint8_t  played_done[UM_SET_COUNT];
    uint32_t pend_game;
    uint32_t pend[2];   /* bit t of 64 */
} um_ach_t;

typedef enum { UM_AWARD_NONE, UM_AWARD_FIRST, UM_AWARD_FULL, UM_AWARD_PENDING } um_award_t;

/* A new deal: the device's count goes up and the game takes the number.
 * Call it after um_new, which clears the struct. */
void um_ach_new_game(um_ach_t *a, um_game_t *g);

/* Set t was just cleared in g (setAward). Returns what that earned, if
 * anything: a First, or UM_AWARD_PENDING once a game for a set this game
 * would make Full when completed. *flood is set when it was the last
 * set's First, which also earns The Yellow River Floods Again. Fulls,
 * and so The Capital is Gone, come only from um_ach_end; *capital is
 * always false here, kept for callers. Either may be NULL. A game with no number yet (saved
 * before games were counted) takes the next one. */
um_award_t um_ach_clear(um_ach_t *a, um_game_t *g, um_set_t t, bool *flood, bool *capital);

/* At the end of g, finished or stuck (endAwards): its clears count as
 * completed, and every Full they make is earned. Returns how many; *last
 * is the last set made Full, *capital set when that felled the Capital.
 * Idempotent: a second call for the same game earns nothing. */
int um_ach_end(um_ach_t *a, um_game_t *g, um_set_t *last, bool *capital);

/* At the end of g, finished or stuck (highAward): true if its points
 * newly earn High score. */
bool um_ach_high(um_ach_t *a, um_game_t *g);

/* At the end of g (winAwards): if every tile was cleared, the wins it
 * newly earns, a bit each (1u << UM_WIN_...). A game whose clears were
 * not all tracked (g->had without UM_HAD_TRACKED: saved by an older
 * build) earns none, since what it cleared before is not known. */
uint8_t um_ach_win(um_ach_t *a, um_game_t *g);

/* Secret s earned in g (secretAward): true if it is new. The caller
 * decides when: PLACE when a game ends with every tile cleared, WAIT
 * when a sort throws a selection away, DESIGN when Meld is pressed on
 * um_pair_grow's selection. */
bool um_ach_secret(um_ach_t *a, um_game_t *g, int s);

int  um_ach_firsts(const um_ach_t *a);   /* sets with a First, of UM_ACH_SETS */
int  um_ach_fulls(const um_ach_t *a);

/* Plausible as read back from storage: no record from a game after the
 * count, no played game twice in a set's list. */
bool um_ach_valid(const um_ach_t *a);

#ifdef __cplusplus
}
#endif
