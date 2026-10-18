/*
 * umtest.c -- the C side of the comparison check.js makes against the
 * web version. Two modes, both writing plain text for check.js to read:
 *
 *   umtest classify            one line of kinds per input line ->
 *                              "<set>|<points>|<bonus>" per line
 *   umtest play <seed> <games> random games: the wall, then each move
 *                              with the state after it
 *
 * and one that checks itself:
 *
 *   umtest valid               um_valid passes games mid-play and fails
 *                              each kind of corruption; exits 1 if not
 *
 * The games are played by a deliberately careless random player, so the
 * odd corners get visited: invalid selections, melds cleared alone,
 * kong upgrades, Add 6 at the limit, discards cycling back, sets
 * discarded.
 *
 * SPDX-License-Identifier: MIT
 */
#include "umcore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t s_rng = 1;
static uint32_t rnd(void) { uint32_t x = s_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return s_rng = x; }
static int below(int n) { return n > 0 ? (int)(rnd() % (uint32_t)n) : 0; }

static int classify_mode(void)
{
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        uint8_t k[64];
        int n = 0;
        for (char *p = strtok(line, " \t\r\n"); p && n < 64; p = strtok(NULL, " \t\r\n")) k[n++] = (uint8_t)atoi(p);
        if (n > UM_MAX_SEL) { puts("|0|0"); continue; }
        const um_set_t t = um_classify(k, n);
        printf("%s|%d|%d\n", um_set_key(t), t ? um_set_value(t, k, n) : 0, t ? (int)um_set_has_bonus(t, k, n) : 0);
    }
    return 0;
}

static void print_state(const um_game_t *g)
{
    /* every state these games reach is one um_valid must take back */
    if (!um_valid(g)) { fprintf(stderr, "um_valid refused a game in play\n"); exit(1); }
    um_sel_t hs = { 0, 0 }, ha = { 0, 0 };
    const bool fs = um_find_set(g, true, &hs);
    const bool fa = um_find_set(g, false, &ha);
    printf("S %d", g->hand_len);
    for (int i = 0; i < g->hand_len; i++) printf(" %d", g->hand[i]);
    printf(" | %d", g->meld_n);
    for (int m = 0; m < g->meld_n; m++) {
        printf(" ");
        for (int i = 0; i < g->meld_len[m]; i++) printf("%s%d", i ? "," : "", g->melds[m][i]);
    }
    printf(" | %d %d", UM_TOTAL - g->wall_pos, g->disc_n);
    for (int i = 0; i < g->disc_n; i++) printf(" %d", g->disc[(g->disc_pos + i) % UM_TOTAL]);
    printf(" | %d %d %d %d | %u %u | %u\n", (int)g->points, (int)g->score, um_penalty(g), (int)g->done,
           fs ? (unsigned)hs.hand : 0u, fa ? (unsigned)ha.hand : 0u, (unsigned)um_lone_honors(g));
}

/* a random selection of n hand tiles, maybe with a meld */
static um_sel_t random_sel(const um_game_t *g, int n, bool with_meld)
{
    um_sel_t s = { 0, 0 };
    int filled[UM_MAX_HAND], nf = 0;
    for (int i = 0; i < g->hand_len; i++) if (g->hand[i] != UM_EMPTY) filled[nf++] = i;
    for (int j = 0; j < n && nf; j++) {
        const int p = below(nf);
        s.hand |= 1u << filled[p];
        filled[p] = filled[--nf];
    }
    if (with_meld && g->meld_n) s.melds = (uint8_t)(1u << below(g->meld_n));
    return s;
}

static void play_mode(uint32_t seed, int games)
{
    for (int gi = 0; gi < games; gi++) {
        static um_game_t g;
        s_rng = seed * 2654435761u + (uint32_t)gi * 40503u + 1u;
        um_new(&g, seed + (uint32_t)gi);
        printf("G %d\nW", gi);
        for (int i = 0; i < UM_TOTAL; i++) printf(" %d", g.wall[i]);
        printf("\n");
        print_state(&g);
        for (int step = 0; step < 400 && !g.done; step++) {
            const int r = below(100);
            char op;
            um_sel_t s = { 0, 0 };
            if (r < 45) {                       /* clear what the hint says */
                op = 'C';
                if (!um_hint(&g, &s)) s = random_sel(&g, 2 + below(4), false);
                um_clear(&g, s, NULL, NULL);
            } else if (r < 55) {                /* clear a meld, alone or with tiles */
                op = 'C';
                s = random_sel(&g, below(3) ? 0 : 1 + below(3), true);
                if (!s.melds && g.meld_n) s.melds = 1;
                um_clear(&g, s, NULL, NULL);
            } else if (r < 70) {                /* meld the hint's set, or upgrade a pung */
                op = 'M';
                bool up = false;
                for (int m = 0; m < g.meld_n && !up; m++) {
                    if (g.meld_len[m] != 3 || g.melds[m][0] != g.melds[m][2]) continue;
                    for (int i = 0; i < g.hand_len; i++)
                        if (g.hand[i] == g.melds[m][0]) { s.hand = 1u << i; s.melds = (uint8_t)(1u << m); up = true; break; }
                }
                if (!up && !um_hint(&g, &s)) s = random_sel(&g, 2 + below(3), false);
                um_meld(&g, s);
            } else if (r < 77) {
                op = 'A';
                um_add_six(&g);
            } else if (r < 84) {
                op = 'D';
                s = random_sel(&g, 6, false);
                um_discard_six(&g, s);
            } else if (r < 87) {                /* discard the hint's set, or a random group */
                op = 'X';
                if (!um_hint(&g, &s)) s = random_sel(&g, 2 + below(3), false);
                um_discard_set(&g, s, NULL);
            } else if (r < 89) {
                op = 'S';
                um_sort(&g);
            } else {                            /* something random, often not a set */
                op = 'C';
                s = random_sel(&g, 2 + below(5), below(4) == 0);
                um_clear(&g, s, NULL, NULL);
            }
            printf("A %c %u %u\n", op, (unsigned)s.hand, (unsigned)s.melds);
            print_state(&g);
        }
        /* play it out by the hint so endings are covered too */
        for (int step = 0; step < 200 && !g.done; step++) {
            um_sel_t s = { 0, 0 };
            if (um_hint(&g, &s)) { um_clear(&g, s, NULL, NULL); printf("A C %u 0\n", (unsigned)s.hand); }
            else if (g.meld_n) { s.melds = 1; um_clear(&g, s, NULL, NULL); printf("A C 0 1\n"); }
            else break;
            print_state(&g);
        }
    }
}

/* um_valid: a game mid-play passes; each corruption, made on a copy of
 * it, fails. The copies are what a save from a different build or a
 * damaged NVS page would hand save_load_game. */
static int valid_mode(void)
{
    static um_game_t g, c;
    int bad = 0;
    um_new(&g, 99);
    /* play a while by the hint, with an Add 6 and a meld, so the hand,
     * the melds, the discards and the log all have something in them */
    um_sel_t s = { 0, 0 };
    for (int i = 0; i < 6 && um_hint(&g, &s); i++) um_clear(&g, s, NULL, NULL);
    um_add_six(&g);
    if (um_hint(&g, &s)) um_meld(&g, s);
    s.hand = 0; s.melds = 0;
    for (int i = 0, n = 0; i < g.hand_len && n < UM_DISCARD_N; i++) if (g.hand[i] != UM_EMPTY) { s.hand |= 1u << i; n++; }
    um_discard_six(&g, s);
    if (!um_valid(&g) || !g.meld_n || !g.disc_n || !g.log_n) { fprintf(stderr, "valid: setup failed\n"); return 1; }

#define BAD(what, stmt) do { c = g; stmt; if (um_valid(&c)) { fprintf(stderr, "valid: passed with %s\n", what); bad++; } } while (0)
    BAD("done not 0 or 1", { uint8_t v = 2; memcpy(&c.done, &v, 1); });
    BAD("wall_pos past the end", c.wall_pos = UM_TOTAL + 1);
    BAD("disc_pos past the end", c.disc_pos = UM_TOTAL);
    BAD("disc_n past the end", c.disc_n = UM_TOTAL + 1);
    BAD("hand_len short", c.hand_len = UM_HAND - 1);
    BAD("hand_len long", c.hand_len = UM_MAX_HAND + 1);
    BAD("meld_n too many", c.meld_n = UM_MAX_MELDS + 1);
    BAD("a meld too long", c.meld_len[0] = UM_MELD_MAX + 1);
    BAD("a meld of one", c.meld_len[0] = 1);
    BAD("a wall kind", c.wall[UM_TOTAL - 1] = UM_KINDS);
    BAD("a discard kind", c.disc[c.disc_pos] = 200);
    BAD("a hand kind", { for (int i = 0; i < c.hand_len; i++) if (c.hand[i] != UM_EMPTY) { c.hand[i] = UM_KINDS; break; } });
    BAD("a negative hand kind", { for (int i = 0; i < c.hand_len; i++) if (c.hand[i] != UM_EMPTY) { c.hand[i] = -2; break; } });
    BAD("a meld kind", c.melds[0][0] = UM_KINDS);
    BAD("a log entry too long", c.log[(c.log_n - 1) % UM_LOG_N].n = UM_MAX_SEL + 1);
    BAD("a log kind", { um_log_t *e = &c.log[(c.log_n - 1) % UM_LOG_N]; e->k[0] = UM_KINDS; });
    BAD("a log type", c.log[(c.log_n - 1) % UM_LOG_N].t = UM_SET_COUNT);
    BAD("five of a kind", { for (int i = 0; i < c.hand_len; i++) if (c.hand[i] != UM_EMPTY) c.hand[i] = 0; });
#undef BAD
    if (bad) return 1;
    printf("valid: a game in play passes, 18 corruptions fail\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "valid")) return valid_mode();
    if (argc >= 2 && !strcmp(argv[1], "classify")) return classify_mode();
    if (argc >= 4 && !strcmp(argv[1], "play")) { play_mode((uint32_t)strtoul(argv[2], NULL, 10), atoi(argv[3])); return 0; }
    fprintf(stderr, "usage: umtest classify | umtest play <seed> <games> | umtest valid\n");
    return 2;
}
