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

/* setAward */
um_award_t um_ach_clear(um_ach_t *a, um_game_t *g, um_set_t t, bool *flood)
{
    if (flood) *flood = false;
    if (t <= UM_NONE || t >= UM_SET_COUNT) return UM_AWARD_NONE;
    const uint32_t gn = this_game(a, g);

    /* the games it was cleared in, within the window, this one last */
    uint32_t *p = a->played[t];
    uint32_t keep[UM_ACH_WINDOW];
    int n = 0;
    for (int i = 0; i < UM_ACH_WINDOW; i++)
        if (p[i] && p[i] + UM_ACH_WINDOW > gn && p[i] != gn && n < UM_ACH_WINDOW - 1) keep[n++] = p[i];
    keep[n++] = gn;
    memset(p, 0, sizeof a->played[t]);
    memcpy(p, keep, n * sizeof keep[0]);

    if (!a->first[t].game) {
        a->first[t] = record(a, g);
        if (!a->flood.game && um_ach_firsts(a) == UM_ACH_SETS) {
            a->flood = record(a, g);
            if (flood) *flood = true;
        }
        return UM_AWARD_FIRST;
    }
    if (!a->full[t].game && n >= UM_ACH_FULL_OF) {
        a->full[t] = record(a, g);
        return UM_AWARD_FULL;
    }
    return UM_AWARD_NONE;
}

/* winAwards */
uint8_t um_ach_win(um_ach_t *a, um_game_t *g)
{
    if (!um_all_cleared(g) || !(g->had & UM_HAD_TRACKED) || !g->log_n) return 0;
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

/* secretAward */
bool um_ach_secret(um_ach_t *a, um_game_t *g, int s)
{
    if (s < 0 || s >= UM_SECRET_N || a->secret[s].game) return false;
    a->secret[s] = record(a, g);
    return true;
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
        for (int i = 0; i < UM_ACH_WINDOW; i++) {
            if (a->played[t][i] > a->games) return false;
            for (int j = 0; j < i; j++) if (a->played[t][i] && a->played[t][i] == a->played[t][j]) return false;
        }
    }
    for (int w = 0; w < UM_WIN_N; w++) if (a->win[w].game > a->games) return false;
    for (int s = 0; s < UM_SECRET_N; s++) if (a->secret[s].game > a->games) return false;
    return a->flood.game <= a->games && (!a->flood.game || um_ach_firsts(a) == UM_ACH_SETS);
}
