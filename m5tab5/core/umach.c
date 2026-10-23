/*
 * umach.c -- see umach.h. Each function names the web version's that it
 * follows, in ../../web/index.html.
 *
 * SPDX-License-Identifier: MIT
 */
#include "umach.h"

#include <string.h>

/* thisGame: a game from before games were counted takes a number */
static uint32_t this_game(um_ach_t *a, um_game_t *g)
{
    if (!g->game_no) g->game_no = ++a->games;
    return g->game_no;
}

/* record */
static um_rec_t record(um_ach_t *a, um_game_t *g)
{
    const um_rec_t r = { this_game(a, g), g->rng };
    return r;
}

/* newGame's nextGame */
void um_ach_new_game(um_ach_t *a, um_game_t *g)
{
    g->game_no = ++a->games;
}

/* dealsIn over the entries of played[t] in mask: the different known
 * seeds, and each unknown one */
static int deals_in(const um_ach_t *a, um_set_t t, uint8_t mask)
{
    const uint8_t known = a->played_known[t];
    int deals = 0;
    for (int i = 0; i < UM_ACH_WINDOW; i++) {
        if (!(mask >> i & 1) || !a->played[t][i]) continue;
        bool again = false;
        if (known >> i & 1)
            for (int j = 0; j < i; j++)
                if ((mask >> j & 1) && a->played[t][j] && (known >> j & 1) && a->played_seed[t][j] == a->played_seed[t][i]) again = true;
        deals += !again;
    }
    return deals;
}

/* setAward */
um_award_t um_ach_clear(um_ach_t *a, um_game_t *g, um_set_t t, bool *flood, bool *capital)
{
    if (flood) *flood = false;
    if (capital) *capital = false;
    if (t <= UM_NONE || t >= UM_SET_COUNT) return UM_AWARD_NONE;
    const uint32_t gn = this_game(a, g);
    /* a loaded deal earns Firsts, and so the river, and nothing else:
     * it is not one of the games Full counts */
    if (g->had & UM_HAD_LOADED) {
        if (a->first[t].game) return UM_AWARD_NONE;
        a->first[t] = record(a, g);
        if (!a->flood.game && um_ach_firsts(a) == UM_ACH_SETS) {
            a->flood = record(a, g);
            if (flood) *flood = true;
        }
        return UM_AWARD_FIRST;
    }

    /* the games it was cleared in, within the window, this one last,
     * each with its seed */
    uint32_t *p = a->played[t], *ps = a->played_seed[t];
    uint32_t keep[UM_ACH_WINDOW], kseed[UM_ACH_WINDOW];
    uint8_t known = 0, done = 0;
    int n = 0;
    for (int i = 0; i < UM_ACH_WINDOW; i++)
        if (p[i] && p[i] + UM_ACH_WINDOW > gn && p[i] != gn && n < UM_ACH_WINDOW - 1) {
            if (a->played_known[t] >> i & 1) known |= (uint8_t)(1u << n);
            if (a->played_done[t] >> i & 1) done |= (uint8_t)(1u << n);
            kseed[n] = ps[i];
            keep[n++] = p[i];
        }
    known |= (uint8_t)(1u << n);     /* this game: its seed known, not yet completed */
    kseed[n] = g->rng;
    keep[n++] = gn;
    memset(p, 0, sizeof a->played[t]);
    memset(ps, 0, sizeof a->played_seed[t]);
    memcpy(p, keep, n * sizeof keep[0]);
    memcpy(ps, kseed, n * sizeof kseed[0]);
    a->played_known[t] = known;
    a->played_done[t] = done;

    /* the deals among the completed games and this one */
    const int deals = deals_in(a, t, (uint8_t)(done | 1u << (n - 1)));

    if (!a->first[t].game) {
        a->first[t] = record(a, g);
        if (!a->flood.game && um_ach_firsts(a) == UM_ACH_SETS) {
            a->flood = record(a, g);
            if (flood) *flood = true;
        }
        return UM_AWARD_FIRST;
    }
    if (!a->full[t].game && deals >= UM_ACH_FULL_OF) {
        /* full once this game is completed: said once a game for each set */
        if (a->pend_game != gn) { a->pend_game = gn; a->pend[0] = a->pend[1] = 0; }
        if (a->pend[t / 32] >> (t % 32) & 1) return UM_AWARD_NONE;
        a->pend[t / 32] |= 1u << (t % 32);
        return UM_AWARD_PENDING;
    }
    return UM_AWARD_NONE;
}

/* endAwards */
int um_ach_end(um_ach_t *a, um_game_t *g, um_set_t *last, bool *capital)
{
    if (last) *last = UM_NONE;
    if (capital) *capital = false;
    if (g->had & UM_HAD_LOADED) return 0;
    const uint32_t gn = this_game(a, g);
    int got = 0;
    for (int t = UM_NONE + 1; t < UM_SET_COUNT; t++) {
        bool mine = false;
        for (int i = 0; i < UM_ACH_WINDOW; i++)
            if (a->played[t][i] == gn && !(a->played_done[t] >> i & 1)) { a->played_done[t] |= (uint8_t)(1u << i); mine = true; }
        if (!mine || a->full[t].game || deals_in(a, (um_set_t)t, a->played_done[t]) < UM_ACH_FULL_OF) continue;
        a->full[t] = record(a, g);
        got++;
        if (last) *last = (um_set_t)t;
        if (!a->capital.game && um_ach_fulls(a) == UM_ACH_SETS) {
            a->capital = record(a, g);
            if (capital) *capital = true;
        }
    }
    return got;
}

/* winAwards */
uint8_t um_ach_win(um_ach_t *a, um_game_t *g)
{
    if (!um_all_cleared(g) || !(g->had & UM_HAD_TRACKED) || (g->had & UM_HAD_LOADED) || !g->log_n) return 0;
    uint8_t got = 0;
    const bool ok[UM_WIN_N] = { !(g->had & UM_HAD_POKER), !(g->had & UM_HAD_PAIR),
                                (g->had & UM_HAD_EVERY_POKER) == UM_HAD_EVERY_POKER };
    for (int w = 0; w < UM_WIN_N; w++) {
        if (a->win[w].game || !ok[w]) continue;
        a->win[w] = record(a, g);
        got |= (uint8_t)(1u << w);
    }
    return got;
}

/* highAward */
bool um_ach_high(um_ach_t *a, um_game_t *g)
{
    if (a->high.game || (g->had & UM_HAD_LOADED) || g->points < UM_ACH_HIGH) return false;
    a->high = record(a, g);
    return true;
}

/* secretAward, for the secrets loading earns */
bool um_ach_load_secret(um_ach_t *a, int s, uint32_t seed)
{
    if (s < UM_SECRET_FIRST_LOAD || s >= UM_SECRET_N || um_ach_secret_rec(a, s)->game) return false;
    um_rec_t *r = um_ach_secret_rec(a, s);
    r->game = a->games;
    r->seed = seed;
    /* a record with game 0 reads as not earned: before any game, it takes 1 */
    if (!r->game) r->game = a->games = 1;
    return true;
}

/* loadedAgain */
bool um_ach_has_seed(const um_ach_t *a, uint32_t seed)
{
    for (int t = UM_NONE + 1; t < UM_SET_COUNT; t++)
        if ((a->first[t].game && a->first[t].seed == seed) || (a->full[t].game && a->full[t].seed == seed)) return true;
    for (int w = 0; w < UM_WIN_N; w++) if (a->win[w].game && a->win[w].seed == seed) return true;
    for (int s = 0; s < UM_SECRET_N; s++) { const um_rec_t *r = um_ach_secret_crec(a, s); if (r->game && r->seed == seed) return true; }
    return (a->flood.game && a->flood.seed == seed) || (a->capital.game && a->capital.seed == seed) || (a->high.game && a->high.seed == seed);
}

/* overdone */
bool um_ach_overdone(um_set_t t, const uint8_t *kinds, int n)
{
    if (!um_set_is_limit(t) || t == UM_FOUR_KONGS || t == UM_SEVEN_PAIRS) return false;
    uint8_t c[UM_KINDS] = { 0 };
    for (int i = 0; i < n; i++) if (kinds[i] < UM_KINDS && ++c[kinds[i]] >= 4) return true;
    return false;
}

/* secretAward */
bool um_ach_secret(um_ach_t *a, um_game_t *g, int s)
{
    if (s < 0 || s >= UM_SECRET_FIRST_LOAD || (g->had & UM_HAD_LOADED) || um_ach_secret_rec(a, s)->game) return false;
    *um_ach_secret_rec(a, s) = record(a, g);
    return true;
}

int um_ach_full_progress(const um_ach_t *a, um_set_t t, uint32_t gn)
{
    if (t <= UM_NONE || t >= UM_SET_COUNT) return 0;
    uint8_t mask = 0;
    for (int i = 0; i < UM_ACH_WINDOW; i++)
        if (a->played[t][i] && a->played[t][i] + UM_ACH_WINDOW > gn && (a->played_done[t] >> i & 1)) mask |= (uint8_t)(1u << i);
    return deals_in(a, t, mask);
}

int um_ach_firsts(const um_ach_t *a)
{
    int n = 0;
    for (int t = UM_NONE + 1; t < UM_SET_COUNT; t++) n += a->first[t].game != 0;
    return n;
}

int um_ach_fulls(const um_ach_t *a)
{
    int n = 0;
    for (int t = UM_NONE + 1; t < UM_SET_COUNT; t++) n += a->full[t].game != 0;
    return n;
}

bool um_ach_valid(const um_ach_t *a)
{
    for (int t = 0; t < UM_SET_COUNT; t++) {
        if (a->first[t].game > a->games || a->full[t].game > a->games) return false;
        if (a->full[t].game && !a->first[t].game) return false;
        if (a->played_known[t] >> UM_ACH_WINDOW || a->played_done[t] >> UM_ACH_WINDOW) return false;
        for (int i = 0; i < UM_ACH_WINDOW; i++) {
            if (a->played[t][i] > a->games) return false;
            if (!a->played[t][i] && ((a->played_known[t] >> i & 1) || a->played_seed[t][i])) return false;
            for (int j = 0; j < i; j++) if (a->played[t][i] && a->played[t][i] == a->played[t][j]) return false;
        }
    }
    for (int w = 0; w < UM_WIN_N; w++) if (a->win[w].game > a->games) return false;
    for (int s = 0; s < UM_SECRET_N; s++) if (um_ach_secret_crec(a, s)->game > a->games) return false;
    if (a->high.game > a->games || a->pend_game > a->games) return false;
    if (a->capital.game > a->games || (a->capital.game && um_ach_fulls(a) != UM_ACH_SETS)) return false;
    return a->flood.game <= a->games && (!a->flood.game || um_ach_firsts(a) == UM_ACH_SETS);
}
