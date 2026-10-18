/*
 * save.c -- see save.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "save.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "save";
#define NS          "umj"
#define GAME_VER    (1u)

typedef struct {
    uint32_t ver;
    uint32_t size;
    um_game_t g;
} blob_t;

static blob_t s_blob;   /* 1 KB: off the stack */

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
    if (err != ESP_OK || len != sizeof s_blob || s_blob.ver != GAME_VER || s_blob.size != sizeof(um_game_t)) return false;
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

void save_load_prefs(save_prefs_t *p)
{
    p->best_points = INT32_MIN;
    p->best_penalty = INT32_MIN;
    p->auto_sort = 0;
    p->lang = 0;
    p->rotation = 1;
    p->theme = 0;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    nvs_get_i32(h, "best_pts", &p->best_points);
    nvs_get_i32(h, "best_pen", &p->best_penalty);
    nvs_get_u8(h, "autosort", &p->auto_sort);
    nvs_get_u8(h, "lang", &p->lang);
    nvs_get_u8(h, "rot", &p->rotation);
    nvs_get_u8(h, "theme", &p->theme);
    if (p->rotation > 3) p->rotation = 1;
    if (p->theme > 1) p->theme = 0;
    nvs_close(h);
}

void save_prefs(const save_prefs_t *p)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "best_pts", p->best_points);
    nvs_set_i32(h, "best_pen", p->best_penalty);
    nvs_set_u8(h, "autosort", p->auto_sort);
    nvs_set_u8(h, "lang", p->lang);
    nvs_set_u8(h, "rot", p->rotation);
    nvs_set_u8(h, "theme", p->theme);
    nvs_commit(h);
    nvs_close(h);
}
