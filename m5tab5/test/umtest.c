/*
 * umtest.c -- the C side of the comparison check.js makes against the
 * web version. Two modes, both writing plain text for check.js to read:
 *
 *   umtest classify            one line of kinds per input line ->
 *                              "<set>|<points>|<bonus>" per line
 *   umtest deal <seed> <n>     the walls dealt for n seeds from <seed>
 *   umtest play <seed> <games> random games: the wall, then each move
 *                              with the state after it
 *
 * and one that checks itself:
 *
 *   umtest code <seed> <games> random games, then each one's replay
 *                              payload, code (light work, 8 bits) and end
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
#include "umach.h"
#include "umreplay.h"

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
        if (n > UM_MAX_SEL) { puts("|0|0|0|0"); continue; }
        const um_set_t t = um_classify(k, n);
        printf("%s|%d|%d|%d|%d", um_set_key(t), t ? um_set_value(t, k, n) : 0, t ? (int)um_set_has_bonus(t, k, n) : 0,
               t ? (int)um_set_holds_pair(t, k, n) : 0, t ? (int)um_ach_overdone(t, k, n) : 0);
        /* and a limit hand's groups, as the celebration shows them */
        if (um_set_is_limit(t)) {
            uint8_t out[UM_MAX_SEL], glen[UM_MAX_GROUPS];
            const int ng = um_limit_groups(t, k, n, out, glen);
            printf("|");
            for (int gi = 0, at = 0; gi < ng; at += glen[gi], gi++) {
                printf("%s", gi ? "/" : "");
                for (int j = 0; j < glen[gi]; j++) printf("%s%d", j ? "," : "", out[at + j]);
            }
        }
        printf("\n");
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
    printf(" | %d %d %d %d | %u %u | %u | %u\n", (int)g->points, (int)g->score, um_penalty(g), (int)g->done,
           fs ? (unsigned)hs.hand : 0u, fa ? (unsigned)ha.hand : 0u, (unsigned)um_lone_honors(g), (unsigned)g->had);
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
    BAD("had with a bit it never sets", c.had |= 0x10);
    /* the move record: these would have the replay code read past it */
    if (!g.moves_ok || g.mv_len < 8) { fprintf(stderr, "valid: setup kept no moves\n"); return 1; }
    BAD("a move cut short", c.mv_len--);
    BAD("a move's count past the record", c.mv[1] = 255);
    BAD("a move's count past a selection", { c.mv[1] = UM_MAX_SEL + 1; c.mv_len = UM_MOVES_BYTES; });
    BAD("a move letter", c.mv[0] = 'Q');
    BAD("a move letter of 0", c.mv[0] = 0);
    BAD("a move's tile kind", { if (c.mv[1]) c.mv[2] = UM_KINDS; else c.mv[0] = 'Z'; });
    BAD("a record of one byte past the moves", c.mv_len++);
#undef BAD
    if (bad) return 1;
    printf("valid: a game in play passes, 26 corruptions fail\n");
    return 0;
}

/* The walls um_new deals for seeds first .. first + n - 1, one per line,
 * for check.js to compare with the web version's deal(seed). */
static void deal_mode(uint32_t first, int n)
{
    static um_game_t g;
    for (int i = 0; i < n; i++) {
        um_new(&g, first + (uint32_t)i);
        for (int j = 0; j < UM_TOTAL; j++) printf("%s%d", j ? " " : "", g.wall[j]);
        printf("\n");
    }
}

/* Where a game is, independent of which slot holds which tile: what a
 * replay must reproduce. check.js prints the web version's the same way. */
static void print_end(const um_game_t *g)
{
    uint8_t h[UM_MAX_HAND];
    int n = 0;
    for (int i = 0; i < g->hand_len; i++) if (g->hand[i] != UM_EMPTY) h[n++] = (uint8_t)g->hand[i];
    for (int i = 1; i < n; i++) { const uint8_t v = h[i]; int j = i - 1; while (j >= 0 && h[j] > v) { h[j + 1] = h[j]; j--; } h[j + 1] = v; }
    printf("E %d %d %d |", (int)g->points, (int)g->score, (int)g->done);
    for (int i = 0; i < n; i++) printf(" %d", h[i]);
    printf(" |");
    for (int m = 0; m < g->meld_n; m++) { printf(" "); for (int i = 0; i < g->meld_len[m]; i++) printf("%s%d", i ? "," : "", g->melds[m][i]); }
    printf(" |");
    for (int i = g->wall_pos; i < UM_TOTAL; i++) printf(" %d", g->wall[i]);
    printf(" |");
    for (int i = 0; i < g->disc_n; i++) printf(" %d", g->disc[(g->disc_pos + i) % UM_TOTAL]);
    printf("\n");
}

/* Random games with every kind of move, as play_mode's player makes them,
 * then the replay code of each. */
static void code_mode(uint32_t seed, int games)
{
    static um_game_t g;
    static char code[UM_CODE_MAX];
    static char head[1], rest[UM_CODE_MAX];
    for (int gi = 0; gi < games; gi++) {
        s_rng = seed * 2654435761u + (uint32_t)gi * 40503u + 7u;
        um_new(&g, seed * 7919u + (uint32_t)gi);
        for (int step = 0; step < 500 && !g.done; step++) {
            const int r = below(100);
            um_sel_t s = { 0, 0 };
            if (r < 45) { if (um_hint(&g, &s)) um_clear(&g, s, NULL, NULL); }
            else if (r < 55) { if (g.meld_n) { s.melds = (uint8_t)(1u << below(g.meld_n)); if (below(2)) { um_sel_t h; if (um_hint(&g, &h)) s.hand = h.hand; } um_clear(&g, s, NULL, NULL); } }
            else if (r < 67) {
                bool up = false;
                for (int m = 0; m < g.meld_n && !up; m++) {
                    if (g.meld_len[m] != 3 || g.melds[m][0] != g.melds[m][2]) continue;
                    for (int i = 0; i < g.hand_len; i++)
                        if (g.hand[i] == g.melds[m][0]) { s.hand = 1u << i; s.melds = (uint8_t)(1u << m); up = true; break; }
                }
                if (!up) {   /* meld a pung if one shows, else the hint's set */
                    uint8_t c[UM_KINDS] = { 0 };
                    for (int i = 0; i < g.hand_len; i++) if (g.hand[i] != UM_EMPTY) c[g.hand[i]]++;
                    for (int k = 0; k < UM_KINDS && !s.hand; k++) if (c[k] >= 3) { const uint8_t t[3] = { (uint8_t)k, (uint8_t)k, (uint8_t)k }; um_sel_by_kinds(&g, t, 3, &s); }
                    if (!s.hand) um_hint(&g, &s);
                }
                um_meld(&g, s);
            }
            else if (r < 74) um_add_six(&g);
            else if (r < 82) { s = random_sel(&g, 6, false); um_discard_six(&g, s); }
            else if (r < 88) { if (um_hint(&g, &s)) um_discard_set(&g, s, NULL); }
            else { s = random_sel(&g, 2 + below(4), below(4) == 0); um_clear(&g, s, NULL, NULL); }
        }
        const int rl = um_replay_pack(&g, head, rest, sizeof rest);
        const int cl = um_replay_code(&g, s_rng, 8, code, sizeof code);
        if (rl < 0 || cl < 0) { printf("X %d\n", gi); continue; }
        printf("R %.1s%.*s %s %u\n", head, rl, rest, code, (unsigned)g.rng);
        print_end(&g);
    }
}

/* ach: a script of games, one line each step --
 *   N <seed>      a new deal (U <seed>: one saved before games were counted;
 *                 L <seed>: one loaded from a code)
 *   C <key>|<k,k,...>   that set cleared
 *   W             the game ends with every tile cleared (its Fulls, then wins)
 *   S             a sort threw a selection away
 *   D             Meld pressed on a melded pair and its third tile
 *   O             a limit hand cleared with four of a kind where three would do
 *   H <points>    the game ends, stuck or not, with these points (its
 *                 Fulls, then High score)
 * and after each, the award and every record, for check.js to compare
 * with the web version's. */
static um_ach_t s_ach;
static um_game_t s_ag;

static void ach_recs(const char *name, const um_rec_t *r)
{
    printf("%s", name);
    for (int t = UM_NONE + 1; t < UM_SET_COUNT; t++) if (r[t].game) printf(";%s=%u/%u", um_set_key((um_set_t)t), (unsigned)r[t].game, (unsigned)r[t].seed);
}

static int ach_mode(void)
{
    static char line[512];
    memset(&s_ach, 0, sizeof s_ach);
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        const char *award = "-";
        int flood = 0, wins = 0, capital = 0, high = 0, ended = 0;
        if (line[0] == 'N' || line[0] == 'U' || line[0] == 'L') {
            memset(&s_ag, 0, sizeof s_ag);
            s_ag.rng = (uint32_t)strtoul(line + 2, NULL, 10);
            s_ag.wall_pos = UM_TOTAL;
            s_ag.hand_len = UM_HAND;
            for (int i = 0; i < UM_MAX_HAND; i++) s_ag.hand[i] = UM_EMPTY;
            if (line[0] == 'N' || line[0] == 'L') { s_ag.had = UM_HAD_TRACKED; um_ach_new_game(&s_ach, &s_ag); }
            if (line[0] == 'L') s_ag.had |= UM_HAD_LOADED;
        } else if (line[0] == 'C') {
            char *bar = strchr(line, '|');
            if (!bar) return 2;
            *bar = 0;
            um_set_t t = UM_NONE;
            for (int i = UM_NONE + 1; i < UM_SET_COUNT; i++) if (!strcmp(um_set_key((um_set_t)i), line + 2)) t = (um_set_t)i;
            uint8_t k[UM_MAX_SEL];
            int n = 0;
            for (char *p = strtok(bar + 1, ","); p && n < UM_MAX_SEL; p = strtok(NULL, ",")) k[n++] = (uint8_t)atoi(p);
            if (!t || um_classify(k, n) != t) { fprintf(stderr, "ach: %s is not %s\n", bar + 1, line + 2); return 2; }
            um_note_set(&s_ag, t, k, n);
            s_ag.log_n++;
            bool f = false, cap = false;
            const um_award_t a = um_ach_clear(&s_ach, &s_ag, t, &f, &cap);
            capital = cap;
            award = a == UM_AWARD_FIRST ? "first" : a == UM_AWARD_FULL ? "full" : a == UM_AWARD_PENDING ? "pending" : "-";
            flood = f;
        } else if (line[0] == 'W') {
            /* A Place for Everything first, as winAwards; the script's
             * games are all cleared when they end */
            bool cap = false;
            ended = um_ach_end(&s_ach, &s_ag, NULL, &cap);
            capital = cap;
            if (s_ag.log_n) um_ach_secret(&s_ach, &s_ag, UM_SECRET_PLACE);
            wins = um_ach_win(&s_ach, &s_ag);
        } else if (line[0] == 'S') {
            um_ach_secret(&s_ach, &s_ag, UM_SECRET_WAIT);
        } else if (line[0] == 'D') {
            um_ach_secret(&s_ach, &s_ag, UM_SECRET_DESIGN);
        } else if (line[0] == 'O') {
            um_ach_secret(&s_ach, &s_ag, UM_SECRET_OVER);
        } else if (line[0] == 'H') {
            s_ag.points = (int32_t)atol(line + 2);
            bool cap = false;
            ended = um_ach_end(&s_ach, &s_ag, NULL, &cap);
            capital = cap;
            high = um_ach_high(&s_ach, &s_ag);
        }
        if (!um_ach_valid(&s_ach)) { fprintf(stderr, "ach: um_ach_valid refused the records\n"); return 1; }
        printf("R %s %d %d %d %d %d %u | ", award, ended, flood, capital, high, wins, (unsigned)s_ach.games);
        ach_recs("first", s_ach.first); printf(" | ");
        ach_recs("full", s_ach.full); printf(" | win");
        static const char *WN[UM_WIN_N] = { "no poker", "no pairs", "all poker" };
        for (int w = 0; w < UM_WIN_N; w++) if (s_ach.win[w].game) printf(";%s=%u/%u", WN[w], (unsigned)s_ach.win[w].game, (unsigned)s_ach.win[w].seed);
        printf(" | flood");
        if (s_ach.flood.game) printf(";%u/%u", (unsigned)s_ach.flood.game, (unsigned)s_ach.flood.seed);
        printf(" | capital");
        if (s_ach.capital.game) printf(";%u/%u", (unsigned)s_ach.capital.game, (unsigned)s_ach.capital.seed);
        printf(" | high");
        if (s_ach.high.game) printf(";%u/%u", (unsigned)s_ach.high.game, (unsigned)s_ach.high.seed);
        printf(" | secret");
        static const char *SN[UM_SECRET_N] = { "place", "wait", "design", "over" };
        for (int s = 0; s < UM_SECRET_N; s++) {
            const um_rec_t *r = um_ach_secret_crec(&s_ach, s);
            if (r->game) printf(";%s=%u/%u", SN[s], (unsigned)r->game, (unsigned)r->seed);
        }
        printf("\n");
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "ach")) return ach_mode();
    if (argc >= 4 && !strcmp(argv[1], "code")) { code_mode((uint32_t)strtoul(argv[2], NULL, 10), atoi(argv[3])); return 0; }
    if (argc >= 4 && !strcmp(argv[1], "deal")) { deal_mode((uint32_t)strtoul(argv[2], NULL, 10), atoi(argv[3])); return 0; }
    if (argc >= 2 && !strcmp(argv[1], "valid")) return valid_mode();
    if (argc >= 2 && !strcmp(argv[1], "classify")) return classify_mode();
    if (argc >= 4 && !strcmp(argv[1], "play")) { play_mode((uint32_t)strtoul(argv[2], NULL, 10), atoi(argv[3])); return 0; }
    fprintf(stderr, "usage: umtest classify | umtest play <seed> <games> | umtest valid\n");
    return 2;
}
