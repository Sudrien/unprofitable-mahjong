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
    uint8_t lang;           /* 0 en, 1 ja, 2 zh */
    uint8_t rotation;       /* gfx quarter turns, 0..3; 1 (landscape) until changed */
} save_prefs_t;

void save_init(void);                       /* nvs_flash_init, erasing an unusable partition */
bool save_load_game(um_game_t *g);          /* false: nothing saved, or saved by another build */
void save_game(const um_game_t *g);
void save_load_prefs(save_prefs_t *p);      /* defaults when nothing is saved */
void save_prefs(const save_prefs_t *p);
