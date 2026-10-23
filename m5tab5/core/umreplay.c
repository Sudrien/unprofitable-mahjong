/*
 * umreplay.c -- see umreplay.h. Each function names the web version's
 * that it follows, in ../../web/index.html.
 *
 * SPDX-License-Identifier: MIT
 */
#include "umreplay.h"

#include <string.h>

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
static const char OPS[] = "ACMKDX";     /* the web version's OPS, in its order */

/* MurmurHash2 (murmur2): defeatist's, word for word, including the
 * memcpy load, since the payload has no alignment to promise.
 * MurmurHash2 is Austin Appleby's, released into the public domain
 * (https://github.com/aappleby/smhasher). */
uint32_t um_murmur2(const void *key, size_t len)
{
    const uint32_t m = 0x5bd1e995u;
    const int r = 24;
    uint32_t h = 0x9747b28cu ^ (uint32_t)len;
    const unsigned char *d = (const unsigned char *)key;
    while (len >= 4) {
        uint32_t k;
        memcpy(&k, d, 4);
        k *= m; k ^= k >> r; k *= m;
        h *= m; h ^= k;
        d += 4; len -= 4;
    }
    switch (len) {
        case 3: h ^= (uint32_t)d[2] << 16; /* fall through */
        case 2: h ^= (uint32_t)d[1] << 8;  /* fall through */
        case 1: h ^= (uint32_t)d[0]; h *= m; break;
        default: break;
    }
    h ^= h >> 13; h *= m; h ^= h >> 15;
    return h;
}

/* ------------------------------------------------------------------ */
/* Mixed-radix packing (Packer): a number in 32-bit limbs, least first.
 * A full move record packs to about 11,000 bits; 400 limbs is 12,800. */

#define LIMBS (400)
typedef struct {
    uint32_t n[LIMBS], m[LIMBS];    /* the number, and the place value of the next digit */
    int nl, ml;                     /* limbs in use */
    int over;                       /* it outgrew LIMBS */
} packer_t;

static void pk_init(packer_t *p) { memset(p, 0, sizeof *p); p->m[0] = 1; p->ml = 1; }

/* put(v, r): n += v * m; m *= r */
static void pk_put(packer_t *p, uint32_t v, uint32_t r)
{
    if (p->over) return;
    uint64_t carry = 0;
    int i = 0;
    for (; i < p->ml || carry; i++) {
        if (i >= LIMBS) { p->over = 1; return; }
        const uint64_t t = (uint64_t)p->n[i] + (i < p->ml ? (uint64_t)p->m[i] * v : 0) + carry;
        p->n[i] = (uint32_t)t;
        carry = t >> 32;
    }
    if (i > p->nl) p->nl = i;
    carry = 0;
    for (i = 0; i < p->ml; i++) {
        const uint64_t t = (uint64_t)p->m[i] * r + carry;
        p->m[i] = (uint32_t)t;
        carry = t >> 32;
    }
    if (carry) {
        if (p->ml >= LIMBS) { p->over = 1; return; }
        p->m[p->ml++] = (uint32_t)carry;
    }
}

/* chars(): six bits at a time from the bottom, until what is left is 0 */
static int pk_chars(const packer_t *p, char *out, int cap)
{
    int bits = 0;
    for (int i = p->nl - 1; i >= 0; i--)
        if (p->n[i]) { bits = i * 32 + 32 - __builtin_clz(p->n[i]); break; }
    const int nd = (bits + 5) / 6;
    if (nd > cap) return -1;
    for (int d = 0; d < nd; d++) {
        const int b = d * 6, li = b / 32, sh = b % 32;
        uint32_t v = p->n[li] >> sh;
        if (sh > 26 && li + 1 < LIMBS) v |= p->n[li + 1] << (32 - sh);
        out[d] = B64[v & 63];
    }
    return nd;
}

/* fixedChars */
static void fixed_chars(uint64_t v, int n, char *out)
{
    for (int i = 0; i < n; i++) { out[i] = B64[v % 64]; v /= 64; }
}

/* ------------------------------------------------------------------ */
/* packGame                                                            */

static um_game_t s_sim;         /* the game replayed from its seed, to read each move's table from */
static packer_t s_mp, s_dp;

int um_replay_pack(const um_game_t *g, char *head, char *rest, size_t cap)
{
    if (!g->moves_ok) return -1;
    um_game_t *sim = &s_sim;
    packer_t *mp = &s_mp, *dp = &s_dp;
    pk_init(mp);
    pk_init(dp);
    um_new(sim, g->rng);

    int count = 0;
    for (int p = 0; p + 2 < g->mv_len; p += 3 + g->mv[p + 1]) count++;
    if (count >= 4096) return -1;
    pk_put(mp, (uint32_t)count, 4096);

    for (int p = 0; p + 2 < g->mv_len; p += 3 + g->mv[p + 1]) {
        const char op = (char)g->mv[p];
        const int n = g->mv[p + 1];
        const uint8_t *k = &g->mv[p + 2], extra = g->mv[p + 2 + n];
        const char *oi = strchr(OPS, op);
        if (!op || !oi) return -1;

        /* the table before the move: each kind on it, ascending, with its count */
        uint8_t cnt[UM_KINDS] = { 0 };
        for (int i = 0; i < sim->hand_len; i++) if (sim->hand[i] != UM_EMPTY) cnt[sim->hand[i]]++;

        pk_put(mp, (uint32_t)(oi - OPS), 6);
        if (op == 'C' || op == 'M') {
            for (int x = 0; x < UM_KINDS; x++) {
                if (!cnt[x]) continue;
                int took = 0;
                for (int j = 0; j < n; j++) if (k[j] == x) took++;
                pk_put(mp, (uint32_t)took, (uint32_t)cnt[x] + 1);
            }
            if (op == 'C') pk_put(mp, extra, 1u << sim->meld_n);
        } else if (op == 'K') {
            pk_put(mp, extra, sim->meld_n);
        } else if (op == 'D' || op == 'X') {
            if (op == 'X') pk_put(dp, (uint32_t)(n - 2), 17);
            uint8_t left[UM_KINDS];
            memcpy(left, cnt, sizeof left);
            for (int j = 0; j < n; j++) {
                int idx = -1, avail = 0;
                for (int x = 0; x < UM_KINDS; x++) {
                    if (!left[x]) continue;
                    if (x == k[j]) idx = avail;
                    avail++;
                }
                if (idx < 0) return -1;
                pk_put(dp, (uint32_t)idx, (uint32_t)avail);
                left[k[j]]--;
            }
        }

        /* and play it, choosing table tiles by kind (applyMove) */
        um_sel_t s = { 0, 0 };
        bool ok;
        if (op == 'A') ok = um_add_six(sim);
        else if (op == 'K') {
            if (extra >= sim->meld_n) return -1;
            const uint8_t want = sim->melds[extra][0];
            ok = um_sel_by_kinds(sim, &want, 1, &s);
            s.melds = (uint8_t)(1u << extra);
            ok = ok && um_meld(sim, s);
        } else {
            ok = um_sel_by_kinds(sim, k, n, &s);
            if (ok && op == 'C') { s.melds = extra; ok = um_clear(sim, s, NULL, NULL); }
            else if (ok && op == 'M') ok = um_meld(sim, s);
            else if (ok && op == 'D') ok = um_discard_six_as(sim, s, k);
            else if (ok && op == 'X') ok = um_discard_set_as(sim, s, k, NULL);
        }
        if (!ok) return -1;
    }
    if (mp->over || dp->over) return -1;

    /* head, then: the moves section's length, the moves, the seed, the discards */
    head[0] = B64[UM_REPLAY_VER];
    if (cap < 2 + 6) return -1;
    const int ml = pk_chars(mp, rest + 2, (int)cap - 2 - 6);
    if (ml < 0 || ml >= 4096) return -1;
    fixed_chars((uint64_t)ml, 2, rest);
    fixed_chars(g->rng, 6, rest + 2 + ml);
    const int dl = pk_chars(dp, rest + 2 + ml + 6, (int)cap - 2 - ml - 6);
    if (dl < 0) return -1;
    return 2 + ml + 6 + dl;
}

/* ------------------------------------------------------------------ */
/* hammingAdd: GF(64) with x^6 + x + 1; two checks per 63 characters  */

static uint8_t s_exp[63], s_log[64];

static void gf_init(void)
{
    if (s_exp[0]) return;
    for (int i = 0, x = 1; i < 63; i++) {
        s_exp[i] = (uint8_t)x;
        s_log[x] = (uint8_t)i;
        x <<= 1;
        if (x & 64) x ^= 0x43;
    }
}

static int gf_mul(int a, int b) { return a && b ? s_exp[(s_log[a] + s_log[b]) % 63] : 0; }

static int b64_val(char c)
{
    const char *p = strchr(B64, c);
    return c && p ? (int)(p - B64) : -1;
}

static char s_pay[UM_CODE_MAX];

/* ------------------------------------------------------------------ */
/* readCode, as far as the seed; and limitInDeal                       */

/* hammingFix: each block of up to 63 characters and its 2 checks; one
 * wrong character in a block is put right. The payload into out. */
static int hamming_fix(const char *code, int len, char *out, int cap, int *fixed)
{
    gf_init();
    int o = 0;
    *fixed = 0;
    for (int b = 0; b < len; b += 65) {
        const int bl = len - b < 65 ? len - b : 65;
        if (bl < 3) return -1;
        int v[65];
        for (int i = 0; i < bl; i++) if ((v[i] = b64_val(code[b + i])) < 0) return -1;
        const int n = bl - 2;
        int s1 = v[n], s2 = v[n + 1];
        for (int i = 0; i < n; i++) { s1 ^= v[i]; s2 ^= gf_mul(v[i], s_exp[i]); }
        if (s1 && s2) {
            const int i = (s_log[s2] - s_log[s1] + 63) % 63;
            if (i >= n) return -1;
            v[i] ^= s1;
            (*fixed)++;
        } else if (s1 || s2) (*fixed)++;       /* a check character itself was wrong */
        if (o + n > cap) return -1;
        for (int i = 0; i < n; i++) out[o++] = B64[v[i]];
    }
    return o;
}

/* fixedValue */
static uint64_t fixed_value(const char *s, int n)
{
    uint64_t v = 0;
    for (int i = n - 1; i >= 0; i--) v = v * 64 + (uint64_t)b64_val(s[i]);
    return v;
}

um_read_t um_replay_read(const char *code, int work, uint32_t *seed, int *fixed)
{
    int fx = 0;
    const int len = (int)strlen(code);
    const int pl = hamming_fix(code, len, s_pay, sizeof s_pay, &fx);
    if (fixed) *fixed = fx;
    if (pl < 1 + 6 + 2 + 6) return UM_READ_UNREADABLE;
    if (b64_val(s_pay[0]) != UM_REPLAY_VER) return UM_READ_VERSION;
    if (work > 0 && um_murmur2(s_pay, (size_t)pl) >> (32 - work) != 0) return UM_READ_WORK;
    const int ml = (int)fixed_value(s_pay + 7, 2);
    if (9 + ml + 6 > pl) return UM_READ_UNREADABLE;
    if (seed) *seed = (uint32_t)fixed_value(s_pay + 9 + ml, 6);
    /* no moves: the moves section is the count 0, which packs to nothing,
     * and nothing follows the seed */
    return ml == 0 && 15 + ml == pl ? UM_READ_DEAL : UM_READ_MOVES;
}

/* Unpacker: the chars back into a number, and digits off its bottom */
typedef struct { uint32_t n[LIMBS]; int nl; } unpacker_t;
static unpacker_t s_mu, s_du;

static bool up_init(unpacker_t *u, const char *s, int len)
{
    memset(u, 0, sizeof *u);
    for (int i = 0; i < len; i++) {
        const int v = b64_val(s[i]), b = i * 6, li = b / 32, sh = b % 32;
        if (v < 0 || li + 1 >= LIMBS) return false;
        u->n[li] |= (uint32_t)v << sh;
        if (sh > 26) u->n[li + 1] |= (uint32_t)v >> (32 - sh);
    }
    u->nl = LIMBS;
    while (u->nl && !u->n[u->nl - 1]) u->nl--;
    return true;
}

/* get(r): v = n % r; n /= r */
static uint32_t up_get(unpacker_t *u, uint32_t r)
{
    if (r <= 1) return 0;
    uint64_t rem = 0;
    for (int i = u->nl - 1; i >= 0; i--) {
        const uint64_t cur = (rem << 32) | u->n[i];
        u->n[i] = (uint32_t)(cur / r);
        rem = cur % r;
    }
    while (u->nl && !u->n[u->nl - 1]) u->nl--;
    return (uint32_t)rem;
}

um_read_t um_replay_read_game(const char *code, int work, um_game_t *g, int *moves, int *fixed)
{
    uint32_t seed = 0;
    if (moves) *moves = 0;
    const um_read_t r = um_replay_read(code, work, &seed, fixed);
    if (r != UM_READ_DEAL && r != UM_READ_MOVES) return r;
    /* um_replay_read left the payload in s_pay */
    const int len = (int)strlen(code);
    int fx;
    const int pl = hamming_fix(code, len, s_pay, sizeof s_pay, &fx);
    const int ml = (int)fixed_value(s_pay + 7, 2);
    if (!up_init(&s_mu, s_pay + 9, ml) || !up_init(&s_du, s_pay + 15 + ml, pl - 15 - ml)) return UM_READ_UNREADABLE;
    um_new(g, seed);
    const int count = (int)up_get(&s_mu, 4096);
    for (int i = 0; i < count; i++) {
        if (moves) *moves = i + 1;
        /* the table before the move: each kind on it, ascending, with its count */
        uint8_t cnt[UM_KINDS] = { 0 };
        for (int j = 0; j < g->hand_len; j++) if (g->hand[j] != UM_EMPTY) cnt[g->hand[j]]++;
        const char op = OPS[up_get(&s_mu, 6)];
        uint8_t k[UM_MAX_SEL > 18 ? UM_MAX_SEL : 18];
        int n = 0;
        uint32_t extra = 0;
        if (op == 'C' || op == 'M') {
            for (int x = 0; x < UM_KINDS; x++) {
                if (!cnt[x]) continue;
                for (uint32_t c = up_get(&s_mu, cnt[x] + 1u); c > 0; c--) {
                    if (n >= (int)sizeof k) return UM_READ_MOVE;
                    k[n++] = (uint8_t)x;
                }
            }
            if (op == 'C') extra = up_get(&s_mu, 1u << g->meld_n);
        } else if (op == 'K') {
            extra = up_get(&s_mu, g->meld_n > 1 ? g->meld_n : 1);
        } else if (op == 'D' || op == 'X') {
            const int want = op == 'D' ? UM_DISCARD_N : (int)up_get(&s_du, 17) + 2;
            uint8_t left[UM_KINDS];
            memcpy(left, cnt, sizeof left);
            for (int j = 0; j < want; j++) {
                uint32_t avail = 0;
                for (int x = 0; x < UM_KINDS; x++) avail += left[x] != 0;
                if (!avail || n >= (int)sizeof k) return UM_READ_MOVE;
                uint32_t pick = up_get(&s_du, avail);
                for (int x = 0; x < UM_KINDS; x++) {
                    if (!left[x]) continue;
                    if (!pick--) { k[n++] = (uint8_t)x; left[x]--; break; }
                }
            }
        }
        /* and play it, choosing table tiles by kind (applyMove) */
        um_sel_t s = { 0, 0 };
        bool ok;
        if (op == 'A') ok = um_add_six(g);
        else if (op == 'K') {
            if (extra >= g->meld_n) return UM_READ_MOVE;
            const uint8_t w = g->melds[extra][0];
            ok = um_sel_by_kinds(g, &w, 1, &s);
            s.melds = (uint8_t)(1u << extra);
            ok = ok && um_meld(g, s);
        } else {
            ok = um_sel_by_kinds(g, k, n, &s);
            if (ok && op == 'C') { s.melds = (uint8_t)extra; ok = um_clear(g, s, NULL, NULL); }
            else if (ok && op == 'M') ok = um_meld(g, s);
            else if (ok && op == 'D') ok = um_discard_six_as(g, s, k);
            else if (ok && op == 'X') ok = um_discard_set_as(g, s, k, NULL);
        }
        if (!ok) return UM_READ_MOVE;
    }
    if (s_mu.nl || s_du.nl) return UM_READ_UNREADABLE;
    if (moves) *moves = count;
    return count ? UM_READ_MOVES : UM_READ_DEAL;
}

bool um_limit_in_deal(uint32_t seed)
{
    um_new(&s_sim, seed);
    uint8_t h[UM_HAND], k[UM_HAND];
    for (int i = 0; i < UM_HAND; i++) h[i] = (uint8_t)s_sim.hand[i];
    for (uint32_t m = 0; m < (1u << UM_HAND); m++) {
        if (__builtin_popcount(m) < 14) continue;
        int n = 0;
        for (int i = 0; i < UM_HAND; i++) if (m >> i & 1) k[n++] = h[i];
        if (um_set_is_limit(um_classify(k, n))) return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */

void (*um_replay_yield)(void);

int um_replay_code(const um_game_t *g, uint32_t nonce_from, int work, char *out, size_t cap)
{
    char head[1];
    const int rl = um_replay_pack(g, head, s_pay + 7, sizeof s_pay - 7);
    if (rl < 0) return -1;
    const int pl = 7 + rl;
    s_pay[0] = head[0];

    /* proveWork: the nonce leads, so no try can reuse the last one's hash */
    uint32_t n = nonce_from;
    for (;;) {
        fixed_chars(n, 6, s_pay + 1);
        if (work <= 0 || um_murmur2(s_pay, (size_t)pl) >> (32 - work) == 0) break;
        if (++n == nonce_from) return -1;       /* every 32-bit nonce tried */
        if (um_replay_yield && (n - nonce_from) % UM_REPLAY_YIELD_EVERY == 0) um_replay_yield();
    }

    gf_init();
    int o = 0;
    for (int b = 0; b < pl; b += 63) {
        const int bl = pl - b < 63 ? pl - b : 63;
        if (o + bl + 2 + 1 > (int)cap) return -1;
        int p1 = 0, p2 = 0;
        for (int i = 0; i < bl; i++) {
            const int d = b64_val(s_pay[b + i]);
            p1 ^= d;
            p2 ^= gf_mul(d, s_exp[i]);
            out[o++] = s_pay[b + i];
        }
        out[o++] = B64[p1];
        out[o++] = B64[p2];
    }
    out[o] = 0;
    return o;
}
