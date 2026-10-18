/*
 * dealcheck.c -- every deal um_new makes can be cleared with no penalty.
 *
 * The README promises it: a deal is built from a plan of sets, then
 * shuffled only while the plan still clears with 18 tiles showing.
 * check.js plays the C deals against the web version but never asks
 * whether the plan works, so a slip in the port of deal() or plan_ok()
 * would pass it. This checks the plan from outside:
 *
 *   - the plan holds every tile once: four of each of the 34 kinds, and
 *     the wall is the same tiles;
 *   - each set in it is a pair, chow, pung or kong by um_classify;
 *   - playing it with the game's own um_clear, set by set from the table
 *     and nothing else (no Add 6, no Discard 6, no melds), every clear
 *     is allowed and the game ends done, with every tile cleared and a
 *     penalty of 0.
 *
 * umcore.c is included rather than linked so the plan deal() built,
 * which is static there, can be read.
 *
 *   dealcheck [deals]
 *
 * SPDX-License-Identifier: MIT
 */
#include "../core/umcore.c"

#include <stdio.h>
#include <stdlib.h>

static int check_deal(uint32_t seed)
{
    static um_game_t g;
    um_new(&g, seed);
    const plan_t *p = &s_plan;

    uint8_t want[UM_KINDS] = { 0 }, got[UM_KINDS] = { 0 };
    int tiles = 0;
    for (int s = 0; s < p->n; s++) {
        const um_set_t t = um_classify(p->k[s], p->len[s]);
        if (t != UM_PAIR && t != UM_CHOW && t != UM_PUNG && t != UM_KONG) {
            fprintf(stderr, "deal %u: plan set %d is not a pair, chow, pung or kong\n", seed, s);
            return 1;
        }
        for (int i = 0; i < p->len[s]; i++) want[p->k[s][i]]++;
        tiles += p->len[s];
    }
    for (int i = 0; i < UM_TOTAL; i++) got[g.wall[i]]++;
    for (int k = 0; k < UM_KINDS; k++)
        if (want[k] != 4 || got[k] != 4) {
            fprintf(stderr, "deal %u: kind %d is %d in the plan, %d in the wall\n", seed, k, want[k], got[k]);
            return 1;
        }
    if (tiles != UM_TOTAL) { fprintf(stderr, "deal %u: the plan holds %d tiles\n", seed, tiles); return 1; }

    for (int s = 0; s < p->n; s++) {
        um_sel_t sel = { 0, 0 };
        for (int i = 0; i < p->len[s]; i++) {
            int slot = -1;
            for (int h = 0; h < g.hand_len && slot < 0; h++)
                if (g.hand[h] == p->k[s][i] && !(sel.hand & (1u << h))) slot = h;
            if (slot < 0) {
                fprintf(stderr, "deal %u: plan set %d needs a kind %d not on the table\n", seed, s, p->k[s][i]);
                return 1;
            }
            sel.hand |= 1u << slot;
        }
        if (!um_clear(&g, sel, NULL, NULL)) {
            fprintf(stderr, "deal %u: um_clear refused plan set %d\n", seed, s);
            return 1;
        }
    }
    if (!g.done || um_in_hand(&g) || um_pool(&g) || g.meld_n || um_penalty(&g) != 0) {
        fprintf(stderr, "deal %u: played out, but done %d, %d on the table, %d to draw, penalty %d\n",
                seed, (int)g.done, um_in_hand(&g), um_pool(&g), um_penalty(&g));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const int deals = argc > 1 ? atoi(argv[1]) : 500;
    int bad = 0;
    for (int d = 0; d < deals; d++) bad += check_deal(0xC0FFEEu + (uint32_t)d * 2654435761u);
    if (bad) { fprintf(stderr, "dealcheck: %d of %d deals cannot be cleared\n", bad, deals); return 1; }
    printf("dealcheck: %d deals, each cleared by its plan with no penalty\n", deals);
    return 0;
}
