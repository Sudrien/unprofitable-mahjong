/*
 * umcore.h -- the rules of Unprofitable Mahjong, with no board in them.
 *
 * A port of the game logic in ../../index.html, which is the reference:
 * when the two disagree, this file is wrong. test/ checks it against the
 * web version's own code, so a change here runs `make -C test`.
 *
 * No ESP-IDF headers and no allocation: a game is one plain struct, so
 * saving is a memcpy into NVS and undo is a copy of the struct.
 *
 * Tiles are "kinds" 0..33: 0-8 Characters 1-9, 9-17 Dots, 18-26 Bamboo,
 * 27-30 East South West North, 31-33 Red Green White dragons. There are
 * four of each.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UM_KINDS        (34)
#define UM_TOTAL        (136)
#define UM_HAND         (18)    /* tiles the table refills to */
#define UM_MAX_HAND     (24)    /* the most that may be out, after Add 6 */
#define UM_ADD_N        (6)
#define UM_ADD_COST     (6)
#define UM_DISCARD_N    (6)
#define UM_DISCARD_COST (6)
#define UM_LEFT_COST    (2)     /* penalty per tile not cleared */
#define UM_MAX_MELDS    (4)
#define UM_MELD_MAX     (9)     /* a full straight is the longest meld */
#define UM_MAX_SEL      (18)    /* four kongs and a pair */
#define UM_COMBO_BONUS  (1)
#define UM_LOG_N        (64)    /* recent clears kept for the rail: a whole ordinary game */
#define UM_EMPTY        (-1)

static inline bool um_is_honor(int k) { return k >= 27; }
static inline bool um_is_edge(int k)  { return k >= 27 || k % 9 == 0 || k % 9 == 8; }

/* Every kind of set, in the order the web version's BASE lists them.
 * UM_NONE is "not a set". */
typedef enum {
    UM_NONE = 0,
    UM_PAIR, UM_CHOW, UM_PUNG, UM_KONG,
    UM_TWO_PAIR, UM_FLUSH, UM_STRAIGHT, UM_FULL_HOUSE, UM_ROYAL_FLUSH,
    UM_RUN4, UM_RUN5, UM_RUN6, UM_RUN7, UM_RUN8, UM_FULL_STRAIGHT,
    UM_DRAGON_SET, UM_WIND_SET,
    UM_MIXED_DOUBLE_CHOW, UM_PURE_DOUBLE_CHOW, UM_MIXED_TRIPLE_CHOW, UM_PURE_TRIPLE_CHOW,
    UM_SEVEN_PAIRS, UM_BIG_THREE_DRAGONS, UM_LITTLE_FOUR_WINDS, UM_ALL_HONORS,
    UM_ALL_TERMINALS, UM_BIG_FOUR_WINDS, UM_THIRTEEN_ORPHANS, UM_NINE_GATES,
    UM_LITTLE_THREE_DRAGONS, UM_TRIPLE_PUNG, UM_MIXED_STRAIGHT, UM_THREE_KONGS,
    UM_ALL_SIMPLES, UM_OUTSIDE_HAND, UM_HALF_FLUSH, UM_ALL_PUNGS, UM_FULL_FLUSH,
    UM_ALL_GREEN, UM_FOUR_KONGS,
    UM_SET_COUNT
} um_set_t;

/* The web version's name for a set ("run of 5", "big four winds"). Used
 * by the tests, and as the English label. */
const char *um_set_key(um_set_t t);
bool um_set_is_limit(um_set_t t);    /* plays the celebration */
bool um_set_is_classic(um_set_t t);  /* named in Title Case */
bool um_set_is_poker(um_set_t t);
bool um_set_meldable(um_set_t t);

/* What a group of tiles makes, and what it scores. `kinds` need not be
 * sorted; n is 2..UM_MAX_SEL. */
um_set_t um_classify(const uint8_t *kinds, int n);
int  um_set_value(um_set_t t, const uint8_t *kinds, int n);
/* A poker hand that also splits into mahjong sets (and so scores them
 * plus UM_COMBO_BONUS). */
bool um_set_has_bonus(um_set_t t, const uint8_t *kinds, int n);

/* A selection: hand slots by bit, melds whole by bit. */
typedef struct {
    uint32_t hand;      /* bit i: hand slot i */
    uint8_t  melds;     /* bit i: meld i, all of it */
} um_sel_t;

typedef struct {
    uint8_t  t;                     /* um_set_t, or one of the two below */
    uint8_t  n;
    int16_t  v;                     /* points, or minus the cost */
    uint8_t  k[UM_MAX_SEL];
} um_log_t;
#define UM_LOG_ADD      (250)       /* um_log_t.t for Add 6 */
#define UM_LOG_DISCARD  (251)       /* and Discard 6 */

typedef struct {
    uint8_t  wall[UM_TOTAL];        /* wall[wall_pos..UM_TOTAL) is still to draw */
    uint8_t  wall_pos;
    uint8_t  disc[UM_TOTAL];        /* a ring: disc_n tiles from disc_pos, oldest first */
    uint8_t  disc_pos, disc_n;
    int8_t   hand[UM_MAX_HAND];     /* UM_EMPTY where a tile was */
    uint8_t  hand_len;              /* UM_HAND, or more after Add 6 */
    uint8_t  melds[UM_MAX_MELDS][UM_MELD_MAX];
    uint8_t  meld_len[UM_MAX_MELDS];
    uint8_t  meld_n;
    int32_t  points;                /* from sets cleared */
    int32_t  score;                 /* shortcuts: minus Add 6 and Discard 6 costs */
    bool     done;                  /* the game has ended and been counted */
    uint32_t rng;                   /* the deal's generator, kept for the record */
    uint16_t log_n;                 /* entries ever logged; the last UM_LOG_N are kept */
    um_log_t log[UM_LOG_N];
} um_game_t;

/* A new solvable deal from `seed` (any value; 0 is remapped). */
void um_new(um_game_t *g, uint32_t seed);
/* A deal from a given wall order, for tests: wall[0..17] become the hand. */
void um_new_from_wall(um_game_t *g, const uint8_t *wall);

int  um_pool(const um_game_t *g);           /* wall + discards still to draw */
int  um_in_hand(const um_game_t *g);
int  um_meld_tiles(const um_game_t *g);
int  um_penalty(const um_game_t *g);        /* starts at -272, 0 for a clean sweep */
void um_remaining(const um_game_t *g, uint8_t out[UM_KINDS]);

/* The selection's tiles: hand slots in slot order, then melds in order.
 * Returns how many; 0 if the selection names an empty slot. */
int  um_sel_kinds(const um_game_t *g, um_sel_t s, uint8_t *out);
int  um_sel_count(const um_game_t *g, um_sel_t s);

/* Actions. Each returns false and changes nothing when not allowed. */
bool um_clear(um_game_t *g, um_sel_t s, um_set_t *t_out, int *v_out);
bool um_can_meld(const um_game_t *g, um_sel_t s);
bool um_meld(um_game_t *g, um_sel_t s);     /* includes pung -> kong */
bool um_is_kong_upgrade(const um_game_t *g, um_sel_t s);
bool um_can_add(const um_game_t *g);
bool um_add_six(um_game_t *g);
bool um_can_discard(const um_game_t *g, um_sel_t s);
bool um_discard_six(um_game_t *g, um_sel_t s);
/* Discard set (discardSet in the web version; a setting, off by default,
 * so the UI decides whether to offer it): a set from the table goes to
 * the discard pile and costs what it would have scored. Six tiles go as
 * Discard 6 instead, which is never dearer. */
bool um_can_discard_set(const um_game_t *g, um_sel_t s);
bool um_discard_set(um_game_t *g, um_sel_t s, int *cost_out);
void um_sort(um_game_t *g);

/* The hint: the best-scoring set on the table, without looking ahead.
 * safe_only skips sets that would strand a wind or dragon. */
bool um_find_set(const um_game_t *g, bool safe_only, um_sel_t *out);
bool um_hint(const um_game_t *g, um_sel_t *out);   /* safe first, then any */

/* Over: nothing left to draw, no melds, and no set on the table. Marks
 * the game done the first time it is true, and returns true then. */
bool um_is_over(const um_game_t *g);
bool um_finish_check(um_game_t *g);

/* Every field a game is read through is in range: tile kinds below
 * UM_KINDS and at most four of each still in play, lengths and counts
 * within their arrays. For a game read back from storage, which may be
 * corrupt or written by a build whose struct differed at the same size;
 * a game made by these functions always passes. */
bool um_valid(const um_game_t *g);

/* Honors with one tile left that can never be cleared, as a bit per
 * honor (bit 0 East .. bit 6 White). */
uint8_t um_lone_honors(const um_game_t *g);

#ifdef __cplusplus
}
#endif
