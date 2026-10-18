/*
 * umcore.c -- see umcore.h. Each function names the web version's
 * function it follows, in ../../index.html, so the two can be read side
 * by side. Where the order of anything matters to the result (the hint's
 * tie-break, which tiles a discard sends first) it follows that file's
 * order exactly, because test/ compares the two move by move.
 *
 * SPDX-License-Identifier: MIT
 */
#include "umcore.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Set names and values (BASE, LIMIT, CLASSIC, POKER, MELDABLE)        */

static const char *const KEYS[UM_SET_COUNT] = {
    [UM_NONE] = "",
    [UM_PAIR] = "pair", [UM_CHOW] = "chow", [UM_PUNG] = "pung", [UM_KONG] = "kong",
    [UM_TWO_PAIR] = "two pair", [UM_FLUSH] = "flush", [UM_STRAIGHT] = "straight",
    [UM_FULL_HOUSE] = "full house", [UM_ROYAL_FLUSH] = "royal flush",
    [UM_RUN4] = "run of 4", [UM_RUN5] = "run of 5", [UM_RUN6] = "run of 6",
    [UM_RUN7] = "run of 7", [UM_RUN8] = "run of 8", [UM_FULL_STRAIGHT] = "full straight",
    [UM_DRAGON_SET] = "dragon set", [UM_WIND_SET] = "wind set",
    [UM_MIXED_DOUBLE_CHOW] = "mixed double chow", [UM_PURE_DOUBLE_CHOW] = "pure double chow",
    [UM_MIXED_TRIPLE_CHOW] = "mixed triple chow", [UM_PURE_TRIPLE_CHOW] = "pure triple chow",
    [UM_SEVEN_PAIRS] = "seven pairs", [UM_BIG_THREE_DRAGONS] = "big three dragons",
    [UM_LITTLE_FOUR_WINDS] = "little four winds", [UM_ALL_HONORS] = "all honors",
    [UM_ALL_TERMINALS] = "all terminals", [UM_BIG_FOUR_WINDS] = "big four winds",
    [UM_THIRTEEN_ORPHANS] = "thirteen orphans", [UM_NINE_GATES] = "nine gates",
    [UM_LITTLE_THREE_DRAGONS] = "little three dragons", [UM_TRIPLE_PUNG] = "triple pung",
    [UM_MIXED_STRAIGHT] = "mixed straight", [UM_THREE_KONGS] = "three kongs",
    [UM_ALL_SIMPLES] = "all simples", [UM_OUTSIDE_HAND] = "outside hand",
    [UM_HALF_FLUSH] = "half flush", [UM_ALL_PUNGS] = "all pungs", [UM_FULL_FLUSH] = "full flush",
    [UM_ALL_GREEN] = "all green", [UM_FOUR_KONGS] = "four kongs",
};

static const int16_t BASE[UM_SET_COUNT] = {
    [UM_PAIR] = 1, [UM_CHOW] = 3, [UM_PUNG] = 4, [UM_KONG] = 8,
    [UM_TWO_PAIR] = 1, [UM_FLUSH] = 1, [UM_STRAIGHT] = 2, [UM_FULL_HOUSE] = 3, [UM_ROYAL_FLUSH] = 10,
    [UM_RUN4] = 6, [UM_RUN5] = 9, [UM_RUN6] = 13, [UM_RUN7] = 18, [UM_RUN8] = 24, [UM_FULL_STRAIGHT] = 32,
    [UM_DRAGON_SET] = 6, [UM_WIND_SET] = 10,
    [UM_MIXED_DOUBLE_CHOW] = 8, [UM_PURE_DOUBLE_CHOW] = 10, [UM_MIXED_TRIPLE_CHOW] = 24, [UM_PURE_TRIPLE_CHOW] = 30,
    [UM_SEVEN_PAIRS] = 40, [UM_BIG_THREE_DRAGONS] = 48, [UM_LITTLE_FOUR_WINDS] = 64, [UM_ALL_HONORS] = 64,
    [UM_ALL_TERMINALS] = 64, [UM_BIG_FOUR_WINDS] = 88, [UM_THIRTEEN_ORPHANS] = 100, [UM_NINE_GATES] = 100,
    [UM_LITTLE_THREE_DRAGONS] = 32, [UM_TRIPLE_PUNG] = 24, [UM_MIXED_STRAIGHT] = 20, [UM_THREE_KONGS] = 48,
    [UM_ALL_SIMPLES] = 20, [UM_OUTSIDE_HAND] = 28, [UM_HALF_FLUSH] = 36, [UM_ALL_PUNGS] = 40, [UM_FULL_FLUSH] = 60,
    [UM_ALL_GREEN] = 100, [UM_FOUR_KONGS] = 100,
};

const char *um_set_key(um_set_t t) { return (t > UM_NONE && t < UM_SET_COUNT) ? KEYS[t] : ""; }

bool um_set_is_limit(um_set_t t)
{
    switch (t) {
    case UM_SEVEN_PAIRS: case UM_BIG_THREE_DRAGONS: case UM_LITTLE_FOUR_WINDS: case UM_ALL_HONORS:
    case UM_ALL_TERMINALS: case UM_BIG_FOUR_WINDS: case UM_THIRTEEN_ORPHANS: case UM_NINE_GATES:
    case UM_ALL_GREEN: case UM_FOUR_KONGS:
        return true;
    default:
        return false;
    }
}

bool um_set_is_classic(um_set_t t)
{
    return t >= UM_LITTLE_THREE_DRAGONS && t <= UM_FULL_FLUSH;
}

bool um_set_is_poker(um_set_t t)
{
    return t == UM_TWO_PAIR || t == UM_FLUSH || t == UM_STRAIGHT || t == UM_FULL_HOUSE;
}

bool um_set_meldable(um_set_t t)
{
    return t == UM_PAIR || t == UM_CHOW || t == UM_PUNG || t == UM_KONG || t == UM_STRAIGHT ||
           (t >= UM_RUN4 && t <= UM_FULL_STRAIGHT) || t == UM_ROYAL_FLUSH;
}

/* ------------------------------------------------------------------ */
/* Classifying a group of tiles (classify, handSplits, tradValue)      */

static const uint8_t ORPHANS[13] = { 0, 8, 9, 17, 18, 26, 27, 28, 29, 30, 31, 32, 33 };
static const uint8_t GREEN[6] = { 19, 20, 21, 23, 25, 32 };

static bool in_list(int k, const uint8_t *list, int n)
{
    for (int i = 0; i < n; i++) if (list[i] == k) return true;
    return false;
}

static void sort_kinds(uint8_t *k, int n)
{
    for (int i = 1; i < n; i++) {
        uint8_t v = k[i];
        int j = i - 1;
        while (j >= 0 && k[j] > v) { k[j + 1] = k[j]; j--; }
        k[j + 1] = v;
    }
}

/* isRun: sorted, one suit, consecutive. */
static bool is_run(const uint8_t *k, int n)
{
    if (n < 1 || um_is_honor(k[n - 1]) || k[0] / 9 != k[n - 1] / 9) return false;
    for (int i = 0; i < n; i++) if (k[i] != k[0] + i) return false;
    return true;
}

/* handSplits, reduced to what classify asks of it: is there a split into
 * four sets and a pair, and is there one whose sets are all kongs / all
 * pungs-or-kongs / all outside (with an outside pair). */
typedef struct {
    uint8_t c[UM_KINDS];
    bool any, all_kongs, all_pungs, outside;
} splits_t;

static void splits_go(splits_t *s, int nsets, int pair, bool kongs, bool pungs, bool outside)
{
    int x = -1;
    for (int i = 0; i < UM_KINDS; i++) if (s->c[i]) { x = i; break; }
    if (x < 0) {
        if (nsets == 4 && pair >= 0) {
            s->any = true;
            if (kongs) s->all_kongs = true;
            if (pungs) s->all_pungs = true;
            if (outside && um_is_edge(pair)) s->outside = true;
        }
        return;
    }
    if (nsets > 4) return;
    const bool e = um_is_edge(x);
    if (pair < 0 && s->c[x] >= 2) {
        s->c[x] -= 2; splits_go(s, nsets, x, kongs, pungs, outside); s->c[x] += 2;
    }
    if (s->c[x] >= 3) {
        s->c[x] -= 3; splits_go(s, nsets + 1, pair, false, pungs, outside && e); s->c[x] += 3;
    }
    if (s->c[x] >= 4) {
        s->c[x] -= 4; splits_go(s, nsets + 1, pair, kongs, pungs, outside && e); s->c[x] += 4;
    }
    if (!um_is_honor(x) && x % 9 <= 6 && s->c[x + 1] && s->c[x + 2]) {
        const bool ce = e || um_is_edge(x + 2);
        s->c[x]--; s->c[x + 1]--; s->c[x + 2]--;
        splits_go(s, nsets + 1, pair, false, false, outside && ce);
        s->c[x]++; s->c[x + 1]++; s->c[x + 2]++;
    }
}

um_set_t um_classify(const uint8_t *kinds, int n)
{
    if (n < 2 || n > UM_MAX_SEL) return UM_NONE;
    uint8_t k[UM_MAX_SEL];
    memcpy(k, kinds, (size_t)n);
    sort_kinds(k, n);
    for (int i = 0; i < n; i++) if (k[i] >= UM_KINDS) return UM_NONE;

    uint8_t cnt[UM_KINDS] = { 0 };
    for (int i = 0; i < n; i++) cnt[k[i]]++;
    uint8_t ks[UM_MAX_SEL], cv[UM_MAX_SEL];
    int nks = 0;
    for (int x = 0; x < UM_KINDS; x++) if (cnt[x]) { ks[nks] = (uint8_t)x; cv[nks] = cnt[x]; nks++; }

    int cv2 = 0, cv3up = 0, cv_lt2 = 0, cv_lt3 = 0;
    for (int i = 0; i < nks; i++) {
        if (cv[i] == 2) cv2++;
        if (cv[i] >= 3) cv3up++;
        if (cv[i] < 2) cv_lt2++;
        if (cv[i] < 3) cv_lt3++;
    }
    bool all_honor = true, all_winds = true, all_dragons = true, all_orph = true, all_terms = true, all_green = true;
    for (int i = 0; i < n; i++) {
        const int x = k[i];
        if (!um_is_honor(x)) all_honor = false;
        if (x < 27 || x > 30) all_winds = false;
        if (x < 31) all_dragons = false;
        if (!in_list(x, ORPHANS, 13)) all_orph = false;
        if (!in_list(x, ORPHANS, 6)) all_terms = false;
        if (!in_list(x, GREEN, 6)) all_green = false;
    }
#define PK(x) (cnt[x] == 3 || cnt[x] == 4)

    /* limit hands */
    if (n == 14 && all_orph) {
        bool every = true;
        for (int i = 0; i < 13; i++) if (!cnt[ORPHANS[i]]) every = false;
        if (every) return UM_THIRTEEN_ORPHANS;
    }
    if (all_winds && PK(27) && PK(28) && PK(29) && PK(30)) return UM_BIG_FOUR_WINDS;
    if (all_winds && nks == 4 && cv3up == 3 && cv2 > 0) return UM_LITTLE_FOUR_WINDS;
    if (all_dragons && PK(31) && PK(32) && PK(33)) return UM_BIG_THREE_DRAGONS;
    if (n == 14 && nks == 7 && cv2 == 7) return UM_SEVEN_PAIRS;
    if (n == 14 && !um_is_honor(k[13]) && k[0] / 9 == k[13] / 9) {
        static const uint8_t gate[9] = { 3, 1, 1, 1, 1, 1, 1, 1, 3 };
        const int s = (k[0] / 9) * 9;
        bool ok = true;
        for (int r = 0; r < 9; r++) if (cnt[s + r] < gate[r]) ok = false;
        if (ok) return UM_NINE_GATES;
    }
    if (n >= 14 && nks == 5 && cv_lt2 == 0 && cv2 == 1) {
        if (all_honor) return UM_ALL_HONORS;
        if (all_terms) return UM_ALL_TERMINALS;
    }

    /* full hands of four sets and a pair (14 to 18 tiles) */
    if (n >= 14) {
        splits_t sp;
        memset(&sp, 0, sizeof sp);
        memcpy(sp.c, cnt, sizeof cnt);
        splits_go(&sp, 0, -1, true, true, true);
        if (sp.any) {
            int suits = 0;
            bool any_honor = false;
            for (int i = 0; i < n; i++) {
                if (um_is_honor(k[i])) any_honor = true;
                else suits |= 1 << (k[i] / 9);
            }
            const bool one_suit = suits == 1 || suits == 2 || suits == 4;
            bool simples = true;
            for (int i = 0; i < n; i++) if (um_is_honor(k[i]) || k[i] % 9 < 1 || k[i] % 9 > 7) simples = false;
            /* pushed in this order; the first of the highest-valued wins */
            const um_set_t order[7] = { UM_FOUR_KONGS, UM_ALL_GREEN, UM_FULL_FLUSH, UM_HALF_FLUSH,
                                        UM_ALL_PUNGS, UM_OUTSIDE_HAND, UM_ALL_SIMPLES };
            const bool hit[7] = { n == 18 && sp.all_kongs, all_green, one_suit && !any_honor,
                                  one_suit && any_honor, sp.all_pungs, sp.outside, simples };
            um_set_t best = UM_NONE;
            for (int i = 0; i < 7; i++)
                if (hit[i] && (best == UM_NONE || BASE[order[i]] > BASE[best])) best = order[i];
            if (best != UM_NONE) return best;
        }
    }
    if (n == 12 && nks == 3 && cv_lt2 == 0 && cv[0] == 4 && cv[1] == 4 && cv[2] == 4) return UM_THREE_KONGS;
    if (all_dragons && nks == 3) {
        const int pks = PK(31) + PK(32) + PK(33);
        if (pks == 2 && (cnt[31] == 2 || cnt[32] == 2 || cnt[33] == 2)) return UM_LITTLE_THREE_DRAGONS;
    }
    if (n >= 9 && n <= 12 && nks == 3 && !um_is_honor(k[n - 1]) && cv_lt3 == 0) {
        if (ks[0] % 9 == ks[1] % 9 && ks[1] % 9 == ks[2] % 9 &&
            ks[0] / 9 != ks[1] / 9 && ks[1] / 9 != ks[2] / 9 && ks[0] / 9 != ks[2] / 9)
            return UM_TRIPLE_PUNG;
    }
    if (n == 9 && nks == 9 && !um_is_honor(k[8])) {
        /* each suit's ranks, compared as whole lists against 1-2-3, 4-5-6, 7-8-9 */
        int have = 0;
        for (int s = 0; s < 3; s++) {
            int rk[9], m = 0;
            for (int i = 0; i < n; i++) if (k[i] / 9 == s) rk[m++] = k[i] % 9;
            if (m == 3 && rk[1] == rk[0] + 1 && rk[2] == rk[0] + 2 && rk[0] % 3 == 0) have |= 1 << (rk[0] / 3);
        }
        if (have == 7) return UM_MIXED_STRAIGHT;
    }
    if (n <= 4 && nks == 1) return n == 2 ? UM_PAIR : n == 3 ? UM_PUNG : UM_KONG;
    if (n == 5 && nks == 2 && ((cv[0] == 3 && cv[1] == 2) || (cv[0] == 2 && cv[1] == 3))) return UM_FULL_HOUSE;
    if (n == 4 && nks == 2 && cv2 == 2) return UM_TWO_PAIR;
    if (n == 5 && is_run(k, n) && k[0] % 9 == 4) return UM_ROYAL_FLUSH;
    if (n >= 3 && is_run(k, n)) {
        if (n == 3) return UM_CHOW;
        if (n == 9) return UM_FULL_STRAIGHT;
        return (um_set_t)(UM_RUN4 + (n - 4));
    }
    if (n == 5 && !um_is_honor(k[4])) {
        int r[5];
        for (int i = 0; i < 5; i++) r[i] = k[i] % 9;
        for (int i = 1; i < 5; i++) { int v = r[i], j = i - 1; while (j >= 0 && r[j] > v) { r[j + 1] = r[j]; j--; } r[j + 1] = v; }
        bool consec = true;
        for (int i = 0; i < 5; i++) if (r[i] != r[0] + i) consec = false;
        if (consec) return UM_STRAIGHT;
        if (k[0] / 9 == k[4] / 9) return UM_FLUSH;
    }
    if (n == 3 && k[0] == 31 && k[1] == 32 && k[2] == 33) return UM_DRAGON_SET;
    if (n == 4 && k[0] == 27 && k[1] == 28 && k[2] == 29 && k[3] == 30) return UM_WIND_SET;
    if ((n == 6 || n == 9) && !um_is_honor(k[n - 1])) {
        const int m = n / 3;
        if (nks == 3 && is_run(ks, 3) && cv[0] == m && cv[1] == m && cv[2] == m)
            return m == 2 ? UM_PURE_DOUBLE_CHOW : UM_PURE_TRIPLE_CHOW;
        uint8_t part[3][UM_MAX_SEL];
        int plen[3] = { 0 }, np = 0, first = -1;
        for (int i = 0; i < n; i++) { const int s = k[i] / 9; part[s][plen[s]++] = k[i]; }
        bool ok = true;
        for (int s = 0; s < 3; s++) {
            if (!plen[s]) continue;
            np++;
            if (plen[s] != 3 || !is_run(part[s], 3)) { ok = false; continue; }
            if (first < 0) first = part[s][0] % 9;
            else if (part[s][0] % 9 != first) ok = false;
        }
        if (ok && np == m) return m == 2 ? UM_MIXED_DOUBLE_CHOW : UM_MIXED_TRIPLE_CHOW;
    }
#undef PK
    return UM_NONE;
}

/* tradValue: the best score splitting the tiles into pairs, chows, pungs
 * and kongs, or -1 when they cannot all be used. */
static int trad_go(uint8_t *c)
{
    int k = -1;
    for (int i = 0; i < UM_KINDS; i++) if (c[i]) { k = i; break; }
    if (k < 0) return 0;
    int best = -1;
    const int e = um_is_edge(k) ? 2 : 1;
#define TRY(cond, take, give, v) do { if (cond) { take; int r = trad_go(c); give; if (r >= 0 && r + (v) > best) best = r + (v); } } while (0)
    TRY(c[k] >= 2, c[k] -= 2, c[k] += 2, BASE[UM_PAIR]);
    TRY(c[k] >= 3, c[k] -= 3, c[k] += 3, BASE[UM_PUNG] * e);
    TRY(c[k] >= 4, c[k] -= 4, c[k] += 4, BASE[UM_KONG] * e);
    TRY(!um_is_honor(k) && k % 9 <= 6 && c[k + 1] && c[k + 2],
        (c[k]--, c[k + 1]--, c[k + 2]--), (c[k]++, c[k + 1]++, c[k + 2]++), BASE[UM_CHOW]);
#undef TRY
    return best;
}

static int trad_value(const uint8_t *kinds, int n)
{
    uint8_t c[UM_KINDS] = { 0 };
    for (int i = 0; i < n; i++) c[kinds[i]]++;
    return trad_go(c);
}

int um_set_value(um_set_t t, const uint8_t *kinds, int n)
{
    if (t <= UM_NONE || t >= UM_SET_COUNT) return 0;
    if (um_set_is_poker(t)) {
        const int tv = trad_value(kinds, n);
        if (tv >= 0) return tv + UM_COMBO_BONUS;
    }
    const int v = BASE[t];
    return ((t == UM_PUNG || t == UM_KONG) && n > 0 && um_is_edge(kinds[0])) ? v * 2 : v;
}

bool um_set_has_bonus(um_set_t t, const uint8_t *kinds, int n)
{
    return um_set_is_poker(t) && trad_value(kinds, n) >= 0;
}

/* limitGroups */
typedef struct {
    uint8_t c[UM_KINDS];
    uint8_t sets[UM_MAX_GROUPS][4], slen[UM_MAX_GROUPS];
    int nsets, pair;
} groups_t;

static bool groups_go(groups_t *g)
{
    int x = -1;
    for (int i = 0; i < UM_KINDS; i++) if (g->c[i]) { x = i; break; }
    if (x < 0) return true;
    if (g->nsets >= UM_MAX_GROUPS - 1) return false;
    uint8_t *set = g->sets[g->nsets];
    if (g->c[x] == 4) {
        g->c[x] -= 4; memset(set, x, 4); g->slen[g->nsets++] = 4;
        if (groups_go(g)) return true;
        g->nsets--; g->c[x] += 4;
    }
    if (g->c[x] >= 3) {
        g->c[x] -= 3; memset(set, x, 3); g->slen[g->nsets++] = 3;
        if (groups_go(g)) return true;
        g->nsets--; g->c[x] += 3;
    }
    if (!um_is_honor(x) && x % 9 <= 6 && g->c[x + 1] && g->c[x + 2]) {
        g->c[x]--; g->c[x + 1]--; g->c[x + 2]--;
        set[0] = (uint8_t)x; set[1] = (uint8_t)(x + 1); set[2] = (uint8_t)(x + 2); g->slen[g->nsets++] = 3;
        if (groups_go(g)) return true;
        g->nsets--; g->c[x]++; g->c[x + 1]++; g->c[x + 2]++;
    }
    if (g->pair < 0 && g->c[x] >= 2) {
        g->c[x] -= 2; g->pair = x;
        if (groups_go(g)) return true;
        g->pair = -1; g->c[x] += 2;
    }
    return false;
}

int um_limit_groups(um_set_t t, const uint8_t *kinds, int n, uint8_t *out, uint8_t *glen)
{
    if (n < 1 || n > UM_MAX_SEL) return 0;
    uint8_t k[UM_MAX_SEL];
    memcpy(k, kinds, (size_t)n);
    sort_kinds(k, n);
    if (t == UM_THIRTEEN_ORPHANS) {
        int m = 0, dup = -1;
        for (int i = 0; i < n; i++) { if (i && k[i] == k[i - 1]) { if (dup < 0) dup = k[i]; continue; } out[m++] = k[i]; }
        if (dup >= 0) out[m++] = (uint8_t)dup;
        glen[0] = (uint8_t)m;
        return 1;
    }
    if (t == UM_SEVEN_PAIRS) {
        int ng = 0;
        for (int i = 0; i + 1 < n && ng < UM_MAX_GROUPS; i += 2) { out[i] = k[i]; out[i + 1] = k[i + 1]; glen[ng++] = 2; }
        return ng;
    }
    static groups_t g;      /* off the stack, like the hint's tables */
    memset(&g, 0, sizeof g);
    g.pair = -1;
    for (int i = 0; i < n; i++) if (k[i] < UM_KINDS) g.c[k[i]]++;
    if (t == UM_NINE_GATES || !groups_go(&g)) { memcpy(out, k, (size_t)n); glen[0] = (uint8_t)n; return 1; }
    int m = 0, ng = 0;
    for (int s = 0; s < g.nsets; s++) { memcpy(out + m, g.sets[s], g.slen[s]); m += g.slen[s]; glen[ng++] = g.slen[s]; }
    if (g.pair >= 0) { out[m++] = (uint8_t)g.pair; out[m++] = (uint8_t)g.pair; glen[ng++] = 2; }
    return ng;
}

/* ------------------------------------------------------------------ */
/* The deal (partSuit, deal)                                           */

static uint32_t rng_next(uint32_t *s)
{
    uint32_t x = *s;    /* xorshift32 */
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* floor(random * n), as rand(n) in the web version */
static int rng_below(uint32_t *s, int n)
{
    return (int)(((uint64_t)rng_next(s) * (uint32_t)n) >> 32);
}

#define PLAN_MAX (UM_TOTAL / 2)
typedef struct {
    uint8_t k[PLAN_MAX][4];
    uint8_t len[PLAN_MAX];
    int n;
} plan_t;

static bool part_suit(plan_t *p, int base, uint32_t *rng)
{
    int c[9] = { 4, 4, 4, 4, 4, 4, 4, 4, 4 };
    for (;;) {
        int r = -1;
        for (int i = 0; i < 9; i++) if (c[i] > 0) { r = i; break; }
        if (r < 0) return true;
        enum { CHOW, PUNG, KONG, PAIR };
        int opts[7], no = 0;
        if (r <= 6 && c[r + 1] > 0 && c[r + 2] > 0) { opts[no++] = CHOW; opts[no++] = CHOW; opts[no++] = CHOW; }
        if (c[r] >= 3) { opts[no++] = PUNG; opts[no++] = PUNG; }
        if (c[r] == 4) opts[no++] = KONG;
        if (c[r] >= 2) opts[no++] = PAIR;
        if (!no) return false;
        const int o = opts[rng_below(rng, no)];
        uint8_t *set = p->k[p->n];
        if (o == CHOW) {
            c[r]--; c[r + 1]--; c[r + 2]--;
            set[0] = (uint8_t)(base + r); set[1] = (uint8_t)(base + r + 1); set[2] = (uint8_t)(base + r + 2);
            p->len[p->n++] = 3;
        } else {
            const int m = o == PUNG ? 3 : o == KONG ? 4 : 2;
            c[r] -= m;
            for (int i = 0; i < m; i++) set[i] = (uint8_t)(base + r);
            p->len[p->n++] = (uint8_t)m;
        }
    }
}

/* With 18 tiles showing and no penalties, can the plan clear set by set? */
static bool plan_ok(const plan_t *p, const uint8_t *flat)
{
    int cnt[UM_KINDS] = { 0 }, pos = 0;
    for (int i = 0; i < UM_HAND; i++) cnt[flat[pos++]]++;
    for (int s = 0; s < p->n; s++) {
        for (int i = 0; i < p->len[s]; i++) if (--cnt[p->k[s][i]] < 0) return false;
        for (int i = 0; i < p->len[s] && pos < UM_TOTAL; i++) cnt[flat[pos++]]++;
    }
    return true;
}

static plan_t s_plan;   /* 340 bytes: kept off the caller's stack */

static void deal(uint8_t *flat, uint32_t *rng)
{
    plan_t *p = &s_plan;
    p->n = 0;
    for (int h = 27; h < 34; h++) {
        /* Math.random() < .4 */
        if (rng_next(rng) < (uint32_t)(0.4 * 4294967296.0)) {
            for (int i = 0; i < 4; i++) p->k[p->n][i] = (uint8_t)h;
            p->len[p->n++] = 4;
        } else {
            for (int j = 0; j < 2; j++) { p->k[p->n][0] = p->k[p->n][1] = (uint8_t)h; p->len[p->n++] = 2; }
        }
    }
    for (int s = 0; s < 3; s++) {
        const int at = p->n;
        while (!part_suit(p, s * 9, rng)) p->n = at;
    }
    for (int i = p->n - 1; i > 0; i--) {       /* shuffle(plan) */
        const int j = rng_below(rng, i + 1);
        uint8_t tk[4]; memcpy(tk, p->k[i], 4); memcpy(p->k[i], p->k[j], 4); memcpy(p->k[j], tk, 4);
        const uint8_t tl = p->len[i]; p->len[i] = p->len[j]; p->len[j] = tl;
    }
    int f = 0;
    for (int s = 0; s < p->n; s++) for (int i = 0; i < p->len[s]; i++) flat[f++] = p->k[s][i];
    for (int i = 0; i < 9000; i++) {
        const int a = rng_below(rng, UM_TOTAL - 1);
        int b = a + 1 + rng_below(rng, 17);
        if (b > UM_TOTAL - 1) b = UM_TOTAL - 1;
        uint8_t t = flat[a]; flat[a] = flat[b]; flat[b] = t;
        if (!plan_ok(p, flat)) { t = flat[a]; flat[a] = flat[b]; flat[b] = t; }
    }
}

void um_new_from_wall(um_game_t *g, const uint8_t *wall)
{
    const uint32_t rng = g->rng;
    memset(g, 0, sizeof *g);
    g->rng = rng;
    memcpy(g->wall, wall, UM_TOTAL);
    for (int i = 0; i < UM_MAX_HAND; i++) g->hand[i] = UM_EMPTY;
    for (int i = 0; i < UM_HAND; i++) g->hand[i] = (int8_t)wall[i];
    g->hand_len = UM_HAND;
    g->wall_pos = UM_HAND;
}

void um_new(um_game_t *g, uint32_t seed)
{
    uint32_t rng = seed ? seed : 0x9E3779B9u;
    uint8_t flat[UM_TOTAL];
    deal(flat, &rng);
    g->rng = seed;
    um_new_from_wall(g, flat);
}

/* ------------------------------------------------------------------ */
/* State                                                               */

int um_pool(const um_game_t *g) { return (UM_TOTAL - g->wall_pos) + g->disc_n; }

int um_in_hand(const um_game_t *g)
{
    int n = 0;
    for (int i = 0; i < g->hand_len; i++) if (g->hand[i] != UM_EMPTY) n++;
    return n;
}

int um_meld_tiles(const um_game_t *g)
{
    int n = 0;
    for (int i = 0; i < g->meld_n; i++) n += g->meld_len[i];
    return n;
}

int um_penalty(const um_game_t *g)
{
    return g->score - UM_LEFT_COST * (um_in_hand(g) + um_pool(g) + um_meld_tiles(g));
}

void um_remaining(const um_game_t *g, uint8_t out[UM_KINDS])
{
    memset(out, 0, UM_KINDS);
    for (int i = g->wall_pos; i < UM_TOTAL; i++) out[g->wall[i]]++;
    for (int i = 0; i < g->disc_n; i++) out[g->disc[(g->disc_pos + i) % UM_TOTAL]]++;
    for (int i = 0; i < g->hand_len; i++) if (g->hand[i] != UM_EMPTY) out[g->hand[i]]++;
    for (int m = 0; m < g->meld_n; m++) for (int i = 0; i < g->meld_len[m]; i++) out[g->melds[m][i]]++;
}

/* draw: the wall first, then the discard pile, oldest first */
static int draw(um_game_t *g)
{
    if (g->wall_pos < UM_TOTAL) return g->wall[g->wall_pos++];
    if (g->disc_n) {
        const int k = g->disc[g->disc_pos];
        g->disc_pos = (uint8_t)((g->disc_pos + 1) % UM_TOTAL);
        g->disc_n--;
        return k;
    }
    return -1;
}

/* settle: pull extra tiles into the gaps, then refill up to 18 */
static void settle(um_game_t *g)
{
    if (g->hand_len > UM_HAND) {
        int8_t extra[UM_MAX_HAND];
        int ne = 0, pe = 0;
        for (int i = UM_HAND; i < g->hand_len; i++) if (g->hand[i] != UM_EMPTY) extra[ne++] = g->hand[i];
        for (int i = 0; i < UM_HAND && pe < ne; i++) if (g->hand[i] == UM_EMPTY) g->hand[i] = extra[pe++];
        int len = UM_HAND;
        while (pe < ne) g->hand[len++] = extra[pe++];
        for (int i = len; i < UM_MAX_HAND; i++) g->hand[i] = UM_EMPTY;
        g->hand_len = (uint8_t)len;
    }
    int n = um_in_hand(g);
    for (int i = 0; i < UM_HAND && n < UM_HAND; i++) {
        if (g->hand[i] != UM_EMPTY) continue;
        const int t = draw(g);
        if (t < 0) break;
        g->hand[i] = (int8_t)t;
        n++;
    }
}

int um_sel_kinds(const um_game_t *g, um_sel_t s, uint8_t *out)
{
    int n = 0;
    for (int i = 0; i < UM_MAX_HAND; i++) {
        if (!(s.hand & (1u << i))) continue;
        if (i >= g->hand_len || g->hand[i] == UM_EMPTY) return 0;
        if (n >= UM_MAX_SEL) return 0;
        out[n++] = (uint8_t)g->hand[i];
    }
    for (int m = 0; m < UM_MAX_MELDS; m++) {
        if (!(s.melds & (1u << m))) continue;
        if (m >= g->meld_n || n + g->meld_len[m] > UM_MAX_SEL) return 0;
        for (int i = 0; i < g->meld_len[m]; i++) out[n++] = g->melds[m][i];
    }
    return n;
}

int um_sel_count(const um_game_t *g, um_sel_t s)
{
    int n = __builtin_popcount(s.hand);
    for (int m = 0; m < g->meld_n; m++) if (s.melds & (1u << m)) n += g->meld_len[m];
    return n;
}

static void log_push(um_game_t *g, int t, const uint8_t *k, int n, int v)
{
    um_log_t *e = &g->log[g->log_n % UM_LOG_N];
    e->t = (uint8_t)t;
    e->n = (uint8_t)n;
    e->v = (int16_t)v;
    if (k && n) { memcpy(e->k, k, (size_t)n); sort_kinds(e->k, n); }
    g->log_n++;
}

/* ------------------------------------------------------------------ */
/* Actions                                                             */

bool um_clear(um_game_t *g, um_sel_t s, um_set_t *t_out, int *v_out)
{
    uint8_t kinds[UM_MAX_SEL];
    const int n = um_sel_kinds(g, s, kinds);
    if (n < 2) return false;
    const um_set_t t = um_classify(kinds, n);
    if (t == UM_NONE) return false;
    /* melds that go, keeping the rest in order */
    int keep = 0;
    for (int m = 0; m < g->meld_n; m++) {
        if (s.melds & (1u << m)) continue;
        if (keep != m) { memcpy(g->melds[keep], g->melds[m], UM_MELD_MAX); g->meld_len[keep] = g->meld_len[m]; }
        keep++;
    }
    g->meld_n = (uint8_t)keep;
    for (int i = 0; i < g->hand_len; i++) if (s.hand & (1u << i)) g->hand[i] = UM_EMPTY;
    settle(g);
    const int v = um_set_value(t, kinds, n);
    g->points += v;
    log_push(g, t, kinds, n, v);
    um_finish_check(g);
    if (t_out) *t_out = t;
    if (v_out) *v_out = v;
    return true;
}

bool um_is_kong_upgrade(const um_game_t *g, um_sel_t s)
{
    if (__builtin_popcount(s.hand) != 1 || __builtin_popcount(s.melds) != 1) return false;
    const int m = __builtin_ctz(s.melds), h = __builtin_ctz(s.hand);
    if (m >= g->meld_n || h >= g->hand_len || g->hand[h] == UM_EMPTY) return false;
    return g->meld_len[m] == 3 && g->melds[m][0] == g->melds[m][2] && g->hand[h] == g->melds[m][0];
}

bool um_can_meld(const um_game_t *g, um_sel_t s)
{
    if (um_is_kong_upgrade(g, s)) return true;
    if (g->meld_n >= UM_MAX_MELDS || !s.hand || s.melds) return false;
    uint8_t kinds[UM_MAX_SEL];
    const int n = um_sel_kinds(g, s, kinds);
    return n >= 2 && n <= UM_MELD_MAX && um_set_meldable(um_classify(kinds, n));
}

bool um_meld(um_game_t *g, um_sel_t s)
{
    if (!um_can_meld(g, s)) return false;
    if (um_is_kong_upgrade(g, s)) {
        const int m = __builtin_ctz(s.melds), h = __builtin_ctz(s.hand);
        g->melds[m][g->meld_len[m]++] = (uint8_t)g->hand[h];
        g->hand[h] = UM_EMPTY;
        settle(g);
        um_finish_check(g);
        return true;
    }
    uint8_t kinds[UM_MAX_SEL];
    const int n = um_sel_kinds(g, s, kinds);
    sort_kinds(kinds, n);
    memcpy(g->melds[g->meld_n], kinds, (size_t)n);
    g->meld_len[g->meld_n++] = (uint8_t)n;
    for (int i = 0; i < g->hand_len; i++) if (s.hand & (1u << i)) g->hand[i] = UM_EMPTY;
    settle(g);
    um_finish_check(g);
    return true;
}

bool um_can_add(const um_game_t *g)
{
    return um_pool(g) > 0 && um_in_hand(g) + UM_ADD_N <= UM_MAX_HAND;
}

bool um_add_six(um_game_t *g)
{
    if (!um_can_add(g)) return false;
    int n = 0;
    for (; n < UM_ADD_N && g->hand_len < UM_MAX_HAND; n++) {
        const int t = draw(g);
        if (t < 0) break;
        g->hand[g->hand_len++] = (int8_t)t;
    }
    g->score -= UM_ADD_COST;
    log_push(g, UM_LOG_ADD, NULL, n, -UM_ADD_COST);
    um_finish_check(g);
    return true;
}

bool um_can_discard(const um_game_t *g, um_sel_t s)
{
    if (s.melds || __builtin_popcount(s.hand) != UM_DISCARD_N || !um_pool(g)) return false;
    uint8_t kinds[UM_MAX_SEL];
    return um_sel_kinds(g, s, kinds) == UM_DISCARD_N;
}

bool um_discard_six(um_game_t *g, um_sel_t s)
{
    if (!um_can_discard(g, s)) return false;
    uint8_t kinds[UM_MAX_SEL];
    const int n = um_sel_kinds(g, s, kinds);
    for (int i = 0; i < g->hand_len; i++) if (s.hand & (1u << i)) g->hand[i] = UM_EMPTY;
    for (int i = 0; i < n; i++) {
        g->disc[(g->disc_pos + g->disc_n) % UM_TOTAL] = kinds[i];
        g->disc_n++;
    }
    settle(g);
    g->score -= UM_DISCARD_COST;
    log_push(g, UM_LOG_DISCARD, kinds, n, -UM_DISCARD_COST);
    um_finish_check(g);
    return true;
}

bool um_can_discard_set(const um_game_t *g, um_sel_t s)
{
    if (s.melds || !s.hand || !um_pool(g)) return false;
    uint8_t kinds[UM_MAX_SEL];
    const int n = um_sel_kinds(g, s, kinds);
    return n >= 2 && um_classify(kinds, n) != UM_NONE;
}

bool um_discard_set(um_game_t *g, um_sel_t s, int *cost_out)
{
    if (!um_can_discard_set(g, s)) return false;
    uint8_t kinds[UM_MAX_SEL];
    const int n = um_sel_kinds(g, s, kinds);
    const int v = um_set_value(um_classify(kinds, n), kinds, n);
    for (int i = 0; i < g->hand_len; i++) if (s.hand & (1u << i)) g->hand[i] = UM_EMPTY;
    for (int i = 0; i < n; i++) {
        g->disc[(g->disc_pos + g->disc_n) % UM_TOTAL] = kinds[i];
        g->disc_n++;
    }
    settle(g);
    g->score -= v;
    log_push(g, UM_LOG_DISCARD, kinds, n, -v);
    um_finish_check(g);
    if (cost_out) *cost_out = v;
    return true;
}

void um_sort(um_game_t *g)
{
    uint8_t t[UM_MAX_HAND];
    int n = 0;
    for (int i = 0; i < g->hand_len; i++) if (g->hand[i] != UM_EMPTY) t[n++] = (uint8_t)g->hand[i];
    sort_kinds(t, n);
    for (int i = 0; i < UM_MAX_HAND; i++) g->hand[i] = i < n ? (int8_t)t[i] : UM_EMPTY;
    g->hand_len = (uint8_t)(n > UM_HAND ? n : UM_HAND);
    um_finish_check(g);
}

/* ------------------------------------------------------------------ */
/* The hint (findSet)                                                  */

/* Candidates, in the web version's order. Static rather than local: a
 * few KB, and this runs on the UI task. Not reentrant, which is fine for
 * one UI task and one test thread. */
#define CAND_MAX (512)
typedef struct {
    uint8_t n;
    uint8_t i[UM_MAX_SEL];   /* hand slots */
} cand_t;
static cand_t s_cand[CAND_MAX];
static int s_ncand;

typedef struct {
    uint8_t n[UM_KINDS];
    uint8_t at[UM_KINDS][UM_MAX_HAND];  /* hand slots holding each kind, ascending */
} byk_t;
static byk_t s_by;

static void cand_push(const uint8_t *slots, int n)
{
    if (s_ncand >= CAND_MAX || n < 1 || n > UM_MAX_SEL) return;
    s_cand[s_ncand].n = (uint8_t)n;
    memcpy(s_cand[s_ncand].i, slots, (size_t)n);
    s_ncand++;
}

bool um_find_set(const um_game_t *g, bool safe_only, um_sel_t *out)
{
    byk_t *by = &s_by;
    memset(by->n, 0, sizeof by->n);
    for (int i = 0; i < g->hand_len; i++)
        if (g->hand[i] != UM_EMPTY) { const int k = g->hand[i]; by->at[k][by->n[k]++] = (uint8_t)i; }
    uint8_t rem[UM_KINDS];
    um_remaining(g, rem);
    s_ncand = 0;
    uint8_t buf[UM_MAX_SEL];
    int nb;
#define HAS(k, m) (by->n[(k)] >= (m))

    /* limit hands first */
    {
        bool all = true;
        for (int j = 0; j < 13; j++) if (!HAS(ORPHANS[j], 1)) all = false;
        if (all) {
            int dup = -1;
            for (int j = 0; j < 13 && dup < 0; j++) if (HAS(ORPHANS[j], 2)) dup = ORPHANS[j];
            if (dup >= 0) {
                nb = 0;
                for (int j = 0; j < 13; j++) buf[nb++] = by->at[ORPHANS[j]][0];
                buf[nb++] = by->at[dup][1];
                cand_push(buf, nb);
            }
        }
    }
    {
        int have = 0, missing = -1;
        for (int w = 27; w <= 30; w++) { if (HAS(w, 3)) have++; else missing = w; }
        if (have == 4) {
            nb = 0;
            for (int w = 27; w <= 30; w++) for (int j = 0; j < 3; j++) buf[nb++] = by->at[w][j];
            cand_push(buf, nb);
        } else if (have == 3 && HAS(missing, 2)) {
            nb = 0;
            for (int w = 27; w <= 30; w++) if (w != missing) for (int j = 0; j < 3; j++) buf[nb++] = by->at[w][j];
            buf[nb++] = by->at[missing][0]; buf[nb++] = by->at[missing][1];
            cand_push(buf, nb);
        }
    }
    if (HAS(31, 3) && HAS(32, 3) && HAS(33, 3)) {
        nb = 0;
        for (int d = 31; d <= 33; d++) for (int j = 0; j < 3; j++) buf[nb++] = by->at[d][j];
        cand_push(buf, nb);
    }
    {
        int np = 0;
        for (int k = 0; k < UM_KINDS; k++) if (HAS(k, 2)) np++;
        if (np >= 7) {
            nb = 0;
            for (int k = 0, c = 0; k < UM_KINDS && c < 7; k++) if (HAS(k, 2)) { buf[nb++] = by->at[k][0]; buf[nb++] = by->at[k][1]; c++; }
            cand_push(buf, nb);
        }
    }
    const int nlim = s_ncand;

    for (int r = 0; r <= 6; r++) {
        bool all = true;
        for (int b = 0; b < 27; b += 9) for (int d = 0; d < 3; d++) if (!HAS(b + r + d, 1)) all = false;
        if (all) {
            nb = 0;
            for (int b = 0; b < 27; b += 9) for (int d = 0; d < 3; d++) buf[nb++] = by->at[b + r + d][0];
            cand_push(buf, nb);
        }
        for (int b = 0; b < 27; b += 9) {
            for (int m = 3; m >= 2; m--) {
                if (HAS(b + r, m) && HAS(b + r + 1, m) && HAS(b + r + 2, m)) {
                    nb = 0;
                    for (int d = 0; d < 3; d++) for (int j = 0; j < m; j++) buf[nb++] = by->at[b + r + d][j];
                    cand_push(buf, nb);
                    break;
                }
            }
            for (int b2 = b + 9; b2 < 27; b2 += 9) {
                bool ok = true;
                for (int d = 0; d < 3; d++) if (!HAS(b + r + d, 1) || !HAS(b2 + r + d, 1)) ok = false;
                if (ok) {
                    nb = 0;
                    for (int d = 0; d < 3; d++) buf[nb++] = by->at[b + r + d][0];
                    for (int d = 0; d < 3; d++) buf[nb++] = by->at[b2 + r + d][0];
                    cand_push(buf, nb);
                }
            }
        }
    }
    for (int s = 0; s < 3; s++) {
        uint8_t best[9], cur[9];
        int nbest = 0, ncur = 0;
        for (int r = 0; r < 9; r++) {
            const int k = s * 9 + r;
            if (HAS(k, 1)) cur[ncur++] = by->at[k][0]; else ncur = 0;
            if (ncur > nbest) { memcpy(best, cur, (size_t)ncur); nbest = ncur; }
        }
        if (nbest >= 4) cand_push(best, nbest);
    }
    /* the big sets, longest first, stable */
    for (int i = nlim + 1; i < s_ncand; i++) {
        cand_t c = s_cand[i];
        int j = i - 1;
        while (j >= nlim && s_cand[j].n < c.n) { s_cand[j + 1] = s_cand[j]; j--; }
        s_cand[j + 1] = c;
    }

    if (HAS(27, 1) && HAS(28, 1) && HAS(29, 1) && HAS(30, 1)) {
        for (int w = 27; w <= 30; w++) buf[w - 27] = by->at[w][0];
        cand_push(buf, 4);
    }
    for (int k = 0; k < UM_KINDS; k++) if (by->n[k] == 4) cand_push(by->at[k], 4);
    for (int t3 = 0; t3 < UM_KINDS; t3++) {
        if (!HAS(t3, 3)) continue;
        int p2 = -1;
        for (int k = 0; k < UM_KINDS && p2 < 0; k++) if (k != t3 && HAS(k, 2)) p2 = k;
        if (p2 >= 0) {
            buf[0] = by->at[t3][0]; buf[1] = by->at[t3][1]; buf[2] = by->at[t3][2];
            buf[3] = by->at[p2][0]; buf[4] = by->at[p2][1];
            cand_push(buf, 5);
            break;
        }
    }
    for (int b = 0; b < 27; b += 9) {
        nb = 0;
        for (int k = b; k < b + 9; k++) for (int j = 0; j < by->n[k] && nb < UM_MAX_SEL; j++) buf[nb++] = by->at[k][j];
        if (nb >= 5) { cand_push(buf, 5); break; }
    }
    for (int r = 0; r <= 4; r++) {
        bool ok = true;
        for (int d = 0; d < 5; d++) {
            int got = -1;
            for (int b = 0; b < 27 && got < 0; b += 9) if (HAS(b + r + d, 1)) got = by->at[b + r + d][0];
            if (got < 0) { ok = false; break; }
            buf[d] = (uint8_t)got;
        }
        if (ok) { cand_push(buf, 5); break; }
    }
    {
        int p1 = -1, p2 = -1;
        for (int k = 0; k < UM_KINDS; k++) if (HAS(k, 2)) { if (p1 < 0) p1 = k; else if (p2 < 0) p2 = k; }
        if (p2 >= 0) {
            buf[0] = by->at[p1][0]; buf[1] = by->at[p1][1]; buf[2] = by->at[p2][0]; buf[3] = by->at[p2][1];
            cand_push(buf, 4);
        }
    }
    if (HAS(31, 1) && HAS(32, 1) && HAS(33, 1)) {
        buf[0] = by->at[31][0]; buf[1] = by->at[32][0]; buf[2] = by->at[33][0];
        cand_push(buf, 3);
    }
    for (int k = 0; k < 27; k++)
        if (k % 9 <= 6 && HAS(k, 1) && HAS(k + 1, 1) && HAS(k + 2, 1)) {
            buf[0] = by->at[k][0]; buf[1] = by->at[k + 1][0]; buf[2] = by->at[k + 2][0];
            cand_push(buf, 3);
        }
    for (int k = 0; k < UM_KINDS; k++)
        if (HAS(k, 3) && !(safe_only && um_is_honor(k) && rem[k] == 4)) cand_push(by->at[k], 3);
    for (int k = 0; k < UM_KINDS; k++)
        if (HAS(k, 2) && !(safe_only && um_is_honor(k) && rem[k] == 3)) cand_push(by->at[k], 2);
#undef HAS

    /* the best-scoring one; the first of equals */
    int best = -1, best_v = -1;
    for (int c = 0; c < s_ncand; c++) {
        uint8_t ks[UM_MAX_SEL];
        for (int j = 0; j < s_cand[c].n; j++) ks[j] = (uint8_t)g->hand[s_cand[c].i[j]];
        const um_set_t t = um_classify(ks, s_cand[c].n);
        if (t == UM_NONE) continue;
        const int v = um_set_value(t, ks, s_cand[c].n);
        if (v > best_v) { best_v = v; best = c; }
    }
    if (best < 0) return false;
    if (out) {
        out->hand = 0;
        out->melds = 0;
        for (int j = 0; j < s_cand[best].n; j++) out->hand |= 1u << s_cand[best].i[j];
    }
    return true;
}

bool um_hint(const um_game_t *g, um_sel_t *out)
{
    return um_find_set(g, true, out) || um_find_set(g, false, out);
}

bool um_is_over(const um_game_t *g)
{
    return !um_pool(g) && !g->meld_n && (!um_in_hand(g) || !um_find_set(g, false, NULL));
}

bool um_finish_check(um_game_t *g)
{
    if (g->done || !um_is_over(g)) return false;
    g->done = true;
    return true;
}

bool um_valid(const um_game_t *g)
{
    uint8_t done;
    memcpy(&done, &g->done, 1);         /* a bool read as anything but 0 or 1 is undefined */
    if (sizeof g->done != 1 || done > 1) return false;
    if (g->wall_pos > UM_TOTAL || g->disc_pos >= UM_TOTAL || g->disc_n > UM_TOTAL) return false;
    if (g->hand_len < UM_HAND || g->hand_len > UM_MAX_HAND) return false;
    if (g->meld_n > UM_MAX_MELDS) return false;
    for (int i = 0; i < UM_TOTAL; i++) if (g->wall[i] >= UM_KINDS) return false;
    for (int i = 0; i < g->disc_n; i++) if (g->disc[(g->disc_pos + i) % UM_TOTAL] >= UM_KINDS) return false;
    for (int i = 0; i < g->hand_len; i++) if (g->hand[i] != UM_EMPTY && (g->hand[i] < 0 || g->hand[i] >= UM_KINDS)) return false;
    for (int m = 0; m < g->meld_n; m++) {
        if (g->meld_len[m] < 2 || g->meld_len[m] > UM_MELD_MAX) return false;
        for (int i = 0; i < g->meld_len[m]; i++) if (g->melds[m][i] >= UM_KINDS) return false;
    }
    /* the rail draws the last UM_LOG_N entries' tiles */
    const int keep = g->log_n < UM_LOG_N ? g->log_n : UM_LOG_N;
    for (int j = 0; j < keep; j++) {
        const um_log_t *e = &g->log[(g->log_n - 1 - j) % UM_LOG_N];
        if (e->t == UM_LOG_ADD) { if (e->n > UM_ADD_N) return false; continue; }
        if (e->t != UM_LOG_DISCARD && (e->t <= UM_NONE || e->t >= UM_SET_COUNT)) return false;
        if (e->n > UM_MAX_SEL) return false;
        for (int i = 0; i < e->n; i++) if (e->k[i] >= UM_KINDS) return false;
    }
    /* now safe to count */
    uint8_t rem[UM_KINDS];
    um_remaining(g, rem);
    for (int k = 0; k < UM_KINDS; k++) if (rem[k] > 4) return false;
    return true;
}

uint8_t um_lone_honors(const um_game_t *g)
{
    uint8_t rem[UM_KINDS], out = 0;
    um_remaining(g, rem);
    for (int k = 27; k < 34; k++) {
        if (rem[k] != 1) continue;
        bool group = true;
        const int lo = k < 31 ? 27 : 31, hi = k < 31 ? 30 : 33;
        for (int x = lo; x <= hi; x++) if (!rem[x]) group = false;
        if (!group) out |= (uint8_t)(1u << (k - 27));
    }
    return out;
}
