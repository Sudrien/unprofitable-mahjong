/*
 * save.h -- the game in progress, the best scores and the settings, in
 * NVS. The game is umcore's struct as it is, behind a version and a size,
 * so a firmware whose struct changed starts a new game instead of reading
 * someone else's bytes as a hand.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "umcore.h"

typedef struct {
    int32_t best_points;    /* INT32_MIN: none yet */
    int32_t best_penalty;   /* INT32_MIN: none yet */
    uint8_t auto_sort;
    uint8_t lang;           /* an i18n_lang_t: 0 English, 1 Simplified Chinese, 2 Japanese */
    uint8_t rotation;       /* gfx quarter turns, 0..3; 1 (landscape) until changed */
    uint8_t theme;          /* 0 light, 1 dark */
} save_prefs_t;

void save_init(void);                       /* nvs_flash_init, erasing an unusable partition */
bool save_load_game(um_game_t *g);          /* false: nothing saved, saved by another build, or not um_valid */
void save_game(const um_game_t *g);
void save_load_prefs(save_prefs_t *p);      /* defaults when nothing is saved */
void save_prefs(const save_prefs_t *p);
