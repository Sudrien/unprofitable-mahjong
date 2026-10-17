/*
 * screentest.c -- run the firmware's real ui.c on the host and save what
 * it draws.
 *
 * ui.c, tiledraw.c, tilefaces.c and umcore.c are compiled as they ship;
 * so is feckless-graphics-handler's gfx.c, through that library's own
 * host shim (its test/shim.[ch] and fake/). Only touch, the clock, the
 * random number and NVS are replaced: touch by a script of taps, and the
 * screen is saved as a PPM before each one.
 *
 *   screentest <out-prefix> x,y x,y ...
 *
 * writes <out-prefix>-00.ppm (the first screen), -01 after the first tap,
 * and so on.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "save.h"
#include "touch.h"
#include "ui.h"

static int s_argc, s_next;
static char **s_argv;
static const char *s_prefix;
static int64_t s_now;
static int s_calls;

uint32_t esp_random(void) { return 20261017u; }
int64_t esp_timer_get_time(void) { return s_now; }

static void dump(int n)
{
    char path[512];
    snprintf(path, sizeof path, "%s-%02d.ppm", s_prefix, n);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    const int w = gfx_w(), h = gfx_h();
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    const uint16_t *fb = gfx_fb();
    for (int i = 0; i < w * h; i++) {
        const uint16_t v = fb[i];
        const unsigned char px[3] = { (unsigned char)((v >> 11) << 3), (unsigned char)(((v >> 5) & 63) << 2), (unsigned char)((v & 31) << 3) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

/* Each call is one pass of ui_run()'s loop. Even calls press the next
 * scripted point, odd calls release it; the screen is saved just before
 * each press, after the previous one was drawn. */
bool touch_get(int *x, int *y)
{
    s_now += 15000;
    const int call = s_calls++;
    if (call < 2) return false;                 /* let the first frame draw */
    if (call % 2) return false;
    const int tap = (call - 2) / 2;
    dump(tap);
    if (s_next >= s_argc) exit(0);
    if (sscanf(s_argv[s_next++], "%d,%d", x, y) != 2) { fprintf(stderr, "bad point\n"); exit(2); }
    if (*x < 0) { s_now += (int64_t)(-*x) * 1000; *x = *y = -1; return false; }   /* "-ms,0": wait */
    return true;
}
void touch_swallow(void) {}

/* NVS: nothing saved, nothing loaded */
void save_init(void) {}
/* SCREENTEST_DEAL=dragons: three dragon pungs on top, for the meld row
 * and the limit-hand celebration. Anything else: a normal deal. */
bool save_load_game(um_game_t *g)
{
    const char *d = getenv("SCREENTEST_DEAL");
    if (!d || strcmp(d, "dragons")) return false;
    uint8_t wall[UM_TOTAL], cnt[UM_KINDS] = { 0 };
    int n = 0;
    for (int k = 31; k <= 33; k++) for (int i = 0; i < 3; i++) { wall[n++] = (uint8_t)k; cnt[k]++; }
    for (int k = 0; k < UM_KINDS; k++) while (cnt[k] < 4) { wall[n++] = (uint8_t)k; cnt[k]++; }
    um_new_from_wall(g, wall);
    return true;
}
void save_game(const um_game_t *g) { (void)g; }
void save_load_prefs(save_prefs_t *p) { p->best_points = INT32_MIN; p->best_penalty = INT32_MIN; p->auto_sort = 0; p->lang = 0; }
void save_prefs(const save_prefs_t *p) { (void)p; }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: screentest <out-prefix> [x,y ...]\n"); return 2; }
    s_prefix = argv[1];
    s_argv = argv;
    s_argc = argc;
    s_next = 2;
    static int panel;
    if (gfx_init(&panel, 720, 1280) != ESP_OK) { fprintf(stderr, "gfx_init failed\n"); return 1; }
    gfx_set_rotation(GFX_ROT_90);
    ui_run();
    return 0;
}
