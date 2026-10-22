/*
 * savecheck.c -- main/save.c on the host, over an in-memory NVS (fake/):
 *
 *   - settings: the defaults with nothing saved, and a round trip;
 *   - a game round trip, byte for byte;
 *   - a game saved by a version 1 build (a 32-entry rail log) is read
 *     into today's 64-entry one: everything before the log as it was,
 *     and the 32 entries that were kept, newest last, renumbered from 0;
 *   - a version 2 game (no move record) is read as it was, with no moves
 *     and moves_ok false, and so are version 1 games;
 *   - a version 3 game (no game number or had) is read with its moves,
 *     and both 0;
 *   - a save of the wrong size or version is turned down;
 *   - the achievements round trip, read as none with nothing saved, and
 *     records that fail um_ach_valid are dropped.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "save.h"
#include "umcore.h"

/* ---- the fake NVS: a few keys, each a value of up to 4 KB ---- */
static struct { char key[16]; size_t len; uint8_t v[4096]; } s_kv[16];
static int s_nkv;

static int find(const char *key, bool make)
{
    for (int i = 0; i < s_nkv; i++) if (!strcmp(s_kv[i].key, key)) return i;
    if (!make || s_nkv == 16) return -1;
    snprintf(s_kv[s_nkv].key, sizeof s_kv[s_nkv].key, "%s", key);
    return s_nkv++;
}
static esp_err_t put(const char *key, const void *v, size_t len)
{
    const int i = find(key, true);
    if (i < 0 || len > sizeof s_kv[i].v) return -1;
    memcpy(s_kv[i].v, v, len);
    s_kv[i].len = len;
    return ESP_OK;
}
static esp_err_t get(const char *key, void *v, size_t len)
{
    const int i = find(key, false);
    if (i < 0) return ESP_ERR_NVS_NOT_FOUND;
    if (s_kv[i].len != len) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(v, s_kv[i].v, len);
    return ESP_OK;
}
const char *esp_err_to_name(esp_err_t e) { (void)e; return "err"; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { s_nkv = 0; return ESP_OK; }
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h) { (void)ns; (void)mode; *h = 1; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *v, size_t len) { (void)h; return put(key, v, len); }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len)
{
    (void)h;
    const int i = find(key, false);
    if (i < 0) return ESP_ERR_NVS_NOT_FOUND;
    if (*len < s_kv[i].len) { *len = s_kv[i].len; return ESP_ERR_NVS_INVALID_LENGTH; }
    memcpy(out, s_kv[i].v, s_kv[i].len);
    *len = s_kv[i].len;
    return ESP_OK;
}
esp_err_t nvs_get_i32(nvs_handle_t h, const char *k, int32_t *v) { (void)h; return get(k, v, sizeof *v); }
esp_err_t nvs_set_i32(nvs_handle_t h, const char *k, int32_t v) { (void)h; return put(k, &v, sizeof v); }
esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v) { (void)h; return get(k, v, sizeof *v); }
esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v) { (void)h; return put(k, &v, sizeof v); }

/* ---- the checks ---- */
static int s_bad;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "savecheck: %s\n", what); s_bad++; } } while (0)

/* a game with at least `entries` on the rail: mostly Discard 6, which
 * puts tiles back into circulation, so the game goes on as long as it
 * needs to; every third move clears the hint's set */
static void play(um_game_t *g, uint32_t seed, int entries)
{
    um_new(g, seed);
    for (int step = 0; step < 1000 && g->log_n < entries && !g->done; step++) {
        um_sel_t s = { 0, 0 };
        if (step % 3 == 0 && um_hint(g, &s)) { um_clear(g, s, NULL, NULL); continue; }
        for (int i = 0, n = 0; i < g->hand_len && n < UM_DISCARD_N; i++) if (g->hand[i] != UM_EMPTY) { s.hand |= 1u << i; n++; }
        if (!um_discard_six(g, s)) break;
    }
}

/* what a version 1 build saved for the same game: its fields up to the
 * log, then a 32-entry ring of the last 32 entries */
#define V1_LOG_N (32)
static void save_v1(const um_game_t *g, uint32_t ver, uint32_t size_field, size_t total)
{
    static uint8_t b[4096];
    memset(b, 0, sizeof b);
    const uint32_t hdr[2] = { ver, size_field };
    memcpy(b, hdr, sizeof hdr);
    memcpy(b + 8, g, offsetof(um_game_t, log));
    um_log_t *ring = (um_log_t *)(b + 8 + offsetof(um_game_t, log));
    const int keep = g->log_n < V1_LOG_N ? g->log_n : V1_LOG_N;
    for (int j = 0; j < keep; j++) {
        const int at = g->log_n - 1 - j;
        ring[at % V1_LOG_N] = g->log[at % UM_LOG_N];
    }
    put("game", b, total);
}

static void check_v1(uint32_t seed, int entries)
{
    static um_game_t src, got;
    play(&src, seed, entries);
    const size_t v1 = (offsetof(um_game_t, log) + V1_LOG_N * sizeof(um_log_t) + 3) / 4 * 4;
    save_v1(&src, 1, (uint32_t)v1, 8 + v1);
    char what[96];
    snprintf(what, sizeof what, "version 1 game, %d entries: not read", src.log_n);
    CHECK(save_load_game(&got), what);
    snprintf(what, sizeof what, "version 1 game, %d entries: fields before the log changed", src.log_n);
    CHECK(!memcmp(&src, &got, offsetof(um_game_t, log_n)), what);
    const int keep = src.log_n < V1_LOG_N ? src.log_n : V1_LOG_N;
    snprintf(what, sizeof what, "version 1 game, %d entries: log_n %d, not %d", src.log_n, got.log_n, keep);
    CHECK(got.log_n == keep, what);
    for (int j = 0; j < keep; j++) {
        const um_log_t *a = &src.log[(src.log_n - 1 - j) % UM_LOG_N], *b = &got.log[(got.log_n - 1 - j) % UM_LOG_N];
        snprintf(what, sizeof what, "version 1 game, %d entries: rail entry %d (newest is 0) changed", src.log_n, j);
        CHECK(a->t == b->t && a->n == b->n && a->v == b->v && !memcmp(a->k, b->k, a->n), what);
    }
    CHECK(um_valid(&got), "version 1 game: not um_valid after reading");
    CHECK(!got.moves_ok && !got.mv_len, "version 1 game: read with moves it never kept");
}

int main(void)
{
    save_init();

    save_prefs_t p;
    save_load_prefs(&p);
    CHECK(p.best_points == INT32_MIN && p.best_penalty == INT32_MIN && p.rotation == 1 && p.discard_set == 0 && p.auto_sort == 0,
          "settings: wrong defaults with nothing saved");
    p.best_points = 301; p.best_penalty = -12; p.auto_sort = 1; p.lang = 2; p.rotation = 3; p.theme = 1; p.discard_set = 1;
    save_prefs(&p);
    save_prefs_t q;
    save_load_prefs(&q);
    CHECK(!memcmp(&p, &q, sizeof p), "settings: not the same after a round trip");

    static um_game_t g, back;
    play(&g, 7, 80);
    CHECK(g.log_n > UM_LOG_N, "setup: the game did not fill the rail");
    save_game(&g);
    CHECK(save_load_game(&back) && !memcmp(&g, &back, sizeof g), "game: not the same after a round trip");

    check_v1(11, 10);           /* fewer than 32 entries: all of them */
    check_v1(12, 45);           /* more: the last 32 */
    check_v1(13, 200);          /* as many as the game makes */

    CHECK(g.moves_ok && g.mv_len > 0 && back.mv_len == g.mv_len && !memcmp(back.mv, g.mv, g.mv_len), "game: the moves did not round trip");

    /* version 2: today's game up to the move record */
    {
        static uint8_t b[4096];
        const size_t v2 = (offsetof(um_game_t, moves_ok) + 3) / 4 * 4;
        const uint32_t hdr[2] = { 2, (uint32_t)v2 };
        memcpy(b, hdr, sizeof hdr);
        memcpy(b + 8, &g, v2);
        put("game", b, 8 + v2);
        CHECK(save_load_game(&back), "version 2 game: not read");
        CHECK(!memcmp(&g, &back, offsetof(um_game_t, moves_ok)), "version 2 game: changed in reading");
        CHECK(!back.moves_ok && !back.mv_len && um_valid(&back), "version 2 game: read with moves it never kept");
    }

    /* version 3: today's game up to the game number */
    {
        static uint8_t b[4096];
        const size_t v3 = (offsetof(um_game_t, game_no) + 3) / 4 * 4;
        const uint32_t hdr[2] = { 3, (uint32_t)v3 };
        g.game_no = 9; g.had |= UM_HAD_PAIR;     /* not in a version 3 save */
        memcpy(b, hdr, sizeof hdr);
        memcpy(b + 8, &g, v3);
        put("game", b, 8 + v3);
        CHECK(save_load_game(&back), "version 3 game: not read");
        CHECK(!memcmp(&g, &back, offsetof(um_game_t, game_no)), "version 3 game: changed in reading");
        CHECK(back.moves_ok && back.mv_len == g.mv_len, "version 3 game: lost its moves");
        CHECK(!back.game_no && !back.had && um_valid(&back), "version 3 game: read with a number or tracking it never had");
    }

    const size_t v1 = (offsetof(um_game_t, log) + V1_LOG_N * sizeof(um_log_t) + 3) / 4 * 4;
    save_v1(&g, 1, (uint32_t)v1 + 4, 8 + v1 + 4);
    CHECK(!save_load_game(&back), "a version 1 save of the wrong size was read");
    save_v1(&g, 3, (uint32_t)sizeof(um_game_t), 8 + sizeof(um_game_t));
    CHECK(!save_load_game(&back), "a version 3 save of version 4's size was read");
    save_v1(&g, 2, (uint32_t)v1, 8 + v1);
    CHECK(!save_load_game(&back), "a version 2 save of version 1's size was read");

    /* the achievements */
    {
        static um_ach_t a, b;
        save_load_ach(&b);
        CHECK(!b.games && !um_ach_firsts(&b), "achievements: some with nothing saved");
        static um_game_t ag;
        um_new(&ag, 5);
        um_ach_new_game(&a, &ag);
        const uint8_t pair[2] = { 4, 4 };
        um_note_set(&ag, UM_PAIR, pair, 2);
        CHECK(um_ach_clear(&a, &ag, UM_PAIR, NULL) == UM_AWARD_FIRST, "achievements: setup: no First");
        save_ach(&a);
        save_load_ach(&b);
        CHECK(!memcmp(&a, &b, sizeof a), "achievements: not the same after a round trip");
        a.first[UM_CHOW].game = a.games + 1;     /* from a game not yet dealt */
        save_ach(&a);
        save_load_ach(&b);
        CHECK(!b.games && !um_ach_firsts(&b), "achievements: records out of range were read");
        put("ach", (const uint8_t *)&a, 12);
        save_load_ach(&b);
        CHECK(!b.games, "achievements: a short save was read");

        /* version 1: two wins, no secrets -- read into the new layout,
         * with the third win and the secrets unearned */
        a.first[UM_CHOW].game = 0;
        a.win[UM_WIN_NO_PAIRS] = (um_rec_t){ a.games, 5 };
        static uint8_t v1[8 + sizeof(uint32_t) + 2 * sizeof a.first + sizeof a.played + 3 * sizeof(um_rec_t)];
        uint8_t *q = v1;
        const uint32_t ver = 1, size = (uint32_t)(sizeof v1 - 8);
        memcpy(q, &ver, 4); q += 4;
        memcpy(q, &size, 4); q += 4;
        memcpy(q, &a.games, 4); q += 4;
        memcpy(q, a.first, sizeof a.first); q += sizeof a.first;
        memcpy(q, a.full, sizeof a.full); q += sizeof a.full;
        memcpy(q, a.played, sizeof a.played); q += sizeof a.played;
        memcpy(q, a.win, 2 * sizeof(um_rec_t)); q += 2 * sizeof(um_rec_t);
        memcpy(q, &a.flood, sizeof a.flood);
        put("ach", v1, sizeof v1);
        save_load_ach(&b);
        CHECK(b.games == a.games && b.first[UM_PAIR].game == a.first[UM_PAIR].game
              && b.win[UM_WIN_NO_PAIRS].game == a.games && !b.win[UM_WIN_ALL_POKER].game
              && !b.secret[UM_SECRET_PLACE].game && !b.secret[UM_SECRET_WAIT].game,
              "achievements: a version 1 save was not read as it was");

        /* version 2: version 3 without the last secret */
        a.secret[UM_SECRET_WAIT] = (um_rec_t){ a.games, 5 };
        static uint8_t v2[8 + sizeof a - sizeof(um_rec_t)];
        const uint32_t ver2 = 2, size2 = (uint32_t)(sizeof a - sizeof(um_rec_t));
        memcpy(v2, &ver2, 4);
        memcpy(v2 + 4, &size2, 4);
        memcpy(v2 + 8, &a, size2);
        put("ach", v2, sizeof v2);
        save_load_ach(&b);
        CHECK(b.games == a.games && b.secret[UM_SECRET_WAIT].game == a.games && !b.secret[UM_SECRET_DESIGN].game,
              "achievements: a version 2 save was not read as it was");
    }

    if (s_bad) return 1;
    printf("savecheck: settings and games round trip, moves too; version 1 and 2 games read without moves, version 3 with them; foreign saves refused; achievements round trip, and versions 1 and 2 read\n");
    return 0;
}
