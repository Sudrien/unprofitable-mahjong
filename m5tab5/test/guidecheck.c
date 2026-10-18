/*
 * guidecheck.c -- print main/guide.c for check.js, which compares it
 * with the web version's GUIDE: each section's heading and size, and for
 * each set its name, note, tile groups, and what the C rules make of the
 * tiles (the set and its points). One line each:
 *
 *   H <first> <count> <heading>
 *   I <set key>|<points>|<group lengths, comma-separated>|<kinds>|<name>|<note>
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>

#include "guide.h"
#include "umcore.h"

int main(void)
{
    for (int s = 0; s < guide_section_count; s++)
        printf("H %d %d %s\n", guide_sections[s].first, guide_sections[s].count, guide_sections[s].head);
    for (int i = 0; i < guide_item_count; i++) {
        const guide_item_t *g = &guide_items[i];
        const um_set_t t = um_classify(g->k, g->n);
        printf("I %s|%d|", um_set_key(t), t ? um_set_value(t, g->k, g->n) : 0);
        int total = 0;
        for (int j = 0; j < g->n_groups; j++) { printf("%s%d", j ? "," : "", g->glen[j]); total += g->glen[j]; }
        printf("|");
        for (int j = 0; j < g->n; j++) printf("%s%d", j ? " " : "", g->k[j]);
        printf("|%s|%s\n", g->name, g->note);
        if (total != g->n) { fprintf(stderr, "guide item %d: groups hold %d tiles, n is %d\n", i, total, g->n); return 1; }
    }
    return 0;
}
