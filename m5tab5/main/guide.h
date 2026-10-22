/*
 * guide.h -- the sets and what they score: the web version's "Sets and
 * scores" guide (GUIDE in ../../web/index.html), as data, for ui.c's list of
 * valid hands.
 *
 * Plain C with no IDF headers, so test/ can print it and check.js can
 * compare it with the web version's GUIDE: the same sections, names,
 * notes and tiles, and the points the C rules give each example are the
 * points the web version shows. Points are not stored here; ui.c asks
 * um_classify and um_set_value, as the web version asks its own rules.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdint.h>

#define GUIDE_MAX_GROUPS (7)    /* seven pairs */
#define GUIDE_MAX_TILES  (18)   /* four kongs and a pair */

typedef struct {
    const char *name;           /* English; the i18n key */
    const char *note;
    uint8_t n_groups;
    uint8_t glen[GUIDE_MAX_GROUPS];
    uint8_t n;                  /* tiles in all */
    uint8_t k[GUIDE_MAX_TILES]; /* the groups, one after another */
} guide_item_t;

typedef struct {
    const char *head;
    uint8_t first, count;       /* into guide_items */
} guide_section_t;

extern const guide_item_t guide_items[];
extern const int guide_item_count;
extern const guide_section_t guide_sections[];
extern const int guide_section_count;
