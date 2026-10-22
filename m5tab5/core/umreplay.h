/*
 * umreplay.h -- a game as a replay code: the web version's packGame,
 * proveWork and hammingAdd (../../web/index.html), for a game the Tab5
 * played, so a QR of its link opens the replay on the web.
 *
 * The code is the web version's, character for character: test/ checks
 * that for the same game both make the same payload, and that the web
 * version reads the Tab5's codes back to the same end. See index.html's
 * "replay codes" for the layout.
 *
 * Plain C, no IDF and no allocation, like umcore.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "umcore.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UM_REPLAY_VER   (1)
#define UM_REPLAY_WORK  (20)    /* leading zero bits of MurmurHash2 */
#define UM_CODE_MAX     (2048)  /* the longest code a full move record makes, and then some */

/* MurmurHash2, 32-bit: defeatist-music-player-for-m5tab5's
 * albumart_cover_hash, seed and all. */
uint32_t um_murmur2(const void *key, size_t len);

/* The payload without its nonce: head (1 char) and the rest, which
 * follows the 6 nonce characters. Returns the rest's length, or -1 if
 * the game has no full move record or a move does not replay. */
int um_replay_pack(const um_game_t *g, char *head, char *rest, size_t cap);

/* The whole code: pack, find a nonce from nonce_from that gives `work`
 * leading zero bits (about 2^work tries), and add the Hamming check
 * characters. out is NUL-terminated; returns its length, or -1. */
int um_replay_code(const um_game_t *g, uint32_t nonce_from, int work, char *out, size_t cap);

/* Reading a code back, as far as the Tab5 needs: the web version's
 * readCode up to the moves. A code with no moves is a deal (its seed);
 * one with moves is a game to watch, which the Tab5 leaves to the web,
 * so its moves are not unpacked or replayed here. */
typedef enum {
    UM_READ_DEAL,       /* no moves: *seed is the deal */
    UM_READ_MOVES,      /* a game with moves: *seed is its deal */
    UM_READ_UNREADABLE, /* not a code, or more than one character wrong in a block */
    UM_READ_VERSION,    /* from a newer version of the game */
    UM_READ_WORK,       /* its proof of work does not hold: not genuine */
} um_read_t;
um_read_t um_replay_read(const char *code, int work, uint32_t *seed, int *fixed);

/* A deal's first 18 tiles already hold a limit hand (some 14 or more of
 * them make one): such a deal is not played from a code (limitInDeal). */
bool um_limit_in_deal(uint32_t seed);

#ifdef __cplusplus
}
#endif
