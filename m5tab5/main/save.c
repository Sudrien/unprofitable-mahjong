/*
 * save.c -- see save.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "save.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "save";
#define NS          "umj"
#define GAME_VER    (4u)    /* 4: the game's number and what it cleared, for achievements; 3: the moves are kept, for replay codes; 2: the rail keeps 64 */

/* Version 1 is version 2 with a 32-entry log: the log is the last member
 * and everything before it is the same, so a version 1 game is read by
 * copying what precedes the log and moving the 32 kept entries to where
 * a 64-entry ring keeps them. A game in progress survives the update. */
/* Version 2 is version 3 without the move record, which is last: it is
 * read as it is, with no moves and moves_ok false, so it plays on but
 * gives no replay code -- its moves before now were never kept. */
/* Version 3 is version 4 without game_no and had, which are last: it is
 * read with both 0, a game with no number that has tracked nothing, so
 * it takes a number at its first achievement and earns no win. */
#define TAIL        offsetof(um_game_t, moves_ok)
#define V3_TAIL     offsetof(um_game_t, game_no)
#define V3_SIZE     ((V3_TAIL + _Alignof(um_game_t) - 1) / _Alignof(um_game_t) * _Alignof(um_game_t))
#define V2_SIZE     ((TAIL + _Alignof(um_game_t) - 1) / _Alignof(um_game_t) * _Alignof(um_game_t))
#define V1_LOG_N    (32)
#define V1_SIZE     ((offsetof(um_game_t, log) + V1_LOG_N * sizeof(um_log_t) + _Alignof(um_game_t) - 1) \
                     / _Alignof(um_game_t) * _Alignof(um_game_t))

typedef struct {
    uint32_t ver;
    uint32_t size;
    um_game_t g;
} blob_t;

static blob_t s_blob;   /* 3 KB: off the stack */

#define ACH_VER     (5u)    /* 5: The Capital is Gone and High score; 4: each played game's seed, for Full; 3: a third secret; 2: a third win and the secrets */
/* Versions 2 to 4 are prefixes of 5: each added only at the end. */
#define ACH_V4_SIZE (offsetof(um_ach_t, capital))
#define ACH_V3_SIZE (offsetof(um_ach_t, played_seed))
#define ACH_V2_SIZE (offsetof(um_ach_t, secret) + 2 * sizeof(um_rec_t))
typedef struct {
    uint32_t ver;
    uint32_t size;
    um_ach_t a;
} ach_blob_t;
static ach_blob_t s_ach_blob;   /* 1.5 KB: off the stack too */

/* Version 1 is version 2 with two wins and no secrets: the same up to
 * win[], then flood. Read field by field into the new layout; the new
 * win and the secrets start unearned. */
typedef struct {
    uint32_t games;
    um_rec_t first[UM_SET_COUNT];
    um_rec_t full[UM_SET_COUNT];
    uint32_t played[UM_SET_COUNT][UM_ACH_WINDOW];
    um_rec_t win[2];
    um_rec_t flood;
} ach_v1_t;
typedef struct {
    uint32_t ver;
    uint32_t size;
    ach_v1_t a;
} ach_blob_v1_t;
static um_log_t s_v1_log[V1_LOG_N];

void save_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS unusable (%s); erasing", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "nvs_flash_init: %s -- nothing will be saved", esp_err_to_name(err));
}

bool save_load_game(um_game_t *g)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = sizeof s_blob;
    const esp_err_t err = nvs_get_blob(h, "game", &s_blob, &len);
    nvs_close(h);
    if (err != ESP_OK) return false;
    if (s_blob.ver == 1 && s_blob.size == V1_SIZE && len == offsetof(blob_t, g) + V1_SIZE) {
        um_game_t *n = &s_blob.g;
        memcpy(s_v1_log, (const uint8_t *)n + offsetof(um_game_t, log), sizeof s_v1_log);
        memset(n->log, 0, sizeof n->log);
        /* log_n counts entries ever logged and the rail keeps the last
         * min(log_n, UM_LOG_N): renumbered from 0, the 32 that were kept
         * are all there is, oldest first */
        const int keep = n->log_n < V1_LOG_N ? n->log_n : V1_LOG_N;
        for (int j = 0; j < keep; j++) n->log[keep - 1 - j] = s_v1_log[(n->log_n - 1 - j) % V1_LOG_N];
        n->log_n = (uint16_t)keep;
        memset((uint8_t *)n + TAIL, 0, sizeof *n - TAIL);
        ESP_LOGI(TAG, "read a version 1 game");
    } else if (s_blob.ver == 3 && s_blob.size == V3_SIZE && len == offsetof(blob_t, g) + V3_SIZE) {
        memset((uint8_t *)&s_blob.g + V3_TAIL, 0, sizeof s_blob.g - V3_TAIL);
        ESP_LOGI(TAG, "read a version 3 game");
    } else if (s_blob.ver == 2 && s_blob.size == V2_SIZE && len == offsetof(blob_t, g) + V2_SIZE) {
        memset((uint8_t *)&s_blob.g + TAIL, 0, sizeof s_blob.g - TAIL);
        ESP_LOGI(TAG, "read a version 2 game");
    } else if (len != sizeof s_blob || s_blob.ver != GAME_VER || s_blob.size != sizeof(um_game_t)) {
        return false;
    }
    /* The version and size catch most other builds' saves, not all: a
     * struct reordered at the same size, or a damaged page, still gets
     * here. Out-of-range kinds would index past the tile names and the
     * hint's tables, so such a game is dropped for a new deal. */
    if (!um_valid(&s_blob.g)) {
        ESP_LOGW(TAG, "saved game out of range; starting a new deal");
        return false;
    }
    memcpy(g, &s_blob.g, sizeof *g);
    return true;
}

void save_game(const um_game_t *g)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    s_blob.ver = GAME_VER;
    s_blob.size = sizeof(um_game_t);
    memcpy(&s_blob.g, g, sizeof *g);
    if (nvs_set_blob(h, "game", &s_blob, sizeof s_blob) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

void save_load_ach(um_ach_t *a)
{
    memset(a, 0, sizeof *a);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof s_ach_blob;
    _Static_assert(sizeof(ach_blob_v1_t) <= sizeof(ach_blob_t), "a version 1 blob fits the buffer");
    const esp_err_t err = nvs_get_blob(h, "ach", &s_ach_blob, &len);
    nvs_close(h);
    if (err != ESP_OK) return;
    if (len == sizeof(ach_blob_v1_t) && s_ach_blob.ver == 1u && s_ach_blob.size == sizeof(ach_v1_t)) {
        static ach_v1_t v1;     /* off the stack */
        /* by offset, as bytes: the blob was read as a version 2 struct */
        memcpy(&v1, (const uint8_t *)&s_ach_blob + offsetof(ach_blob_v1_t, a), sizeof v1);
        memset(&s_ach_blob.a, 0, sizeof s_ach_blob.a);
        s_ach_blob.a.games = v1.games;
        memcpy(s_ach_blob.a.first, v1.first, sizeof v1.first);
        memcpy(s_ach_blob.a.full, v1.full, sizeof v1.full);
        memcpy(s_ach_blob.a.played, v1.played, sizeof v1.played);
        memcpy(s_ach_blob.a.win, v1.win, sizeof v1.win);
        s_ach_blob.a.flood = v1.flood;
        if (!um_ach_valid(&s_ach_blob.a)) {
            ESP_LOGW(TAG, "saved achievements (version 1) unreadable; starting over");
            return;
        }
        ESP_LOGI(TAG, "achievements read from version 1");
        memcpy(a, &s_ach_blob.a, sizeof *a);
        return;
    }
    if (((s_ach_blob.ver == 2u && s_ach_blob.size == ACH_V2_SIZE) || (s_ach_blob.ver == 3u && s_ach_blob.size == ACH_V3_SIZE)
         || (s_ach_blob.ver == 4u && s_ach_blob.size == ACH_V4_SIZE))
        && len == offsetof(ach_blob_t, a) + s_ach_blob.size) {
        /* what it did not have starts empty: the later secrets, the
         * Capital and High score unearned, the played games' seeds unknown */
        memset((uint8_t *)&s_ach_blob.a + s_ach_blob.size, 0, sizeof s_ach_blob.a - s_ach_blob.size);
        if (!um_ach_valid(&s_ach_blob.a)) {
            ESP_LOGW(TAG, "saved achievements (version %u) unreadable; starting over", (unsigned)s_ach_blob.ver);
            return;
        }
        ESP_LOGI(TAG, "achievements read from version %u", (unsigned)s_ach_blob.ver);
        memcpy(a, &s_ach_blob.a, sizeof *a);
        return;
    }
    if (len != sizeof s_ach_blob || s_ach_blob.ver != ACH_VER || s_ach_blob.size != sizeof(um_ach_t) || !um_ach_valid(&s_ach_blob.a)) {
        ESP_LOGW(TAG, "saved achievements unreadable; starting over");
        return;
    }
    memcpy(a, &s_ach_blob.a, sizeof *a);
}

void save_ach(const um_ach_t *a)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    s_ach_blob.ver = ACH_VER;
    s_ach_blob.size = sizeof(um_ach_t);
    memcpy(&s_ach_blob.a, a, sizeof *a);
    if (nvs_set_blob(h, "ach", &s_ach_blob, sizeof s_ach_blob) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

void save_load_prefs(save_prefs_t *p)
{
    p->best_points = INT32_MIN;
    p->best_penalty = INT32_MIN;
    p->auto_sort = 0;
    p->discard_set = 0;
    p->lang = 0;
    p->rotation = 1;
    p->theme = 0;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    nvs_get_i32(h, "best_pts", &p->best_points);
    nvs_get_i32(h, "best_pen", &p->best_penalty);
    nvs_get_u8(h, "autosort", &p->auto_sort);
    nvs_get_u8(h, "discset", &p->discard_set);
    nvs_get_u8(h, "lang", &p->lang);
    nvs_get_u8(h, "rot", &p->rotation);
    nvs_get_u8(h, "theme", &p->theme);
    if (p->rotation > 3) p->rotation = 1;
    if (p->theme > 1) p->theme = 0;
    if (p->discard_set > 1) p->discard_set = 0;
    nvs_close(h);
}

void save_prefs(const save_prefs_t *p)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "best_pts", p->best_points);
    nvs_set_i32(h, "best_pen", p->best_penalty);
    nvs_set_u8(h, "autosort", p->auto_sort);
    nvs_set_u8(h, "discset", p->discard_set);
    nvs_set_u8(h, "lang", p->lang);
    nvs_set_u8(h, "rot", p->rotation);
    nvs_set_u8(h, "theme", p->theme);
    nvs_commit(h);
    nvs_close(h);
}
