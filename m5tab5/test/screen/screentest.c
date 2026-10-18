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
 *   screentest <out-prefix> x,y x,y>x2,y2 ...
 *
 * writes <out-prefix>-00.ppm (the first screen), -01 after the first tap
 * (or drag, x,y>x2,y2), and so on.
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

/* Each call is one pass of ui_run()'s loop. Each scripted point becomes
 * a few passes: a tap is a press and a release; "x,y>x2,y2" is a drag,
 * pressed at x,y, moved to x2,y2 over eight passes and released there;
 * "-ms,0" waits. The screen is saved before each point's first pass,
 * after the one before it was drawn. */
#define QMAX (12)
static int s_qn, s_qi, s_qx[QMAX], s_qy[QMAX], s_point;
static bool s_qd[QMAX];

static void queue(bool down, int x, int y) { s_qd[s_qn] = down; s_qx[s_qn] = x; s_qy[s_qn] = y; s_qn++; }

bool touch_get(int *x, int *y)
{
    s_now += 15000;
    if (s_calls++ < 2) return false;            /* let the first frame draw */
    if (s_qi >= s_qn) {
        dump(s_point++);
        if (s_next >= s_argc) exit(0);
        const char *a = s_argv[s_next++];
        int x1, y1, x2, y2;
        s_qn = s_qi = 0;
        if (sscanf(a, "%d,%d>%d,%d", &x1, &y1, &x2, &y2) == 4) {
            for (int i = 0; i <= 8; i++) queue(true, x1 + (x2 - x1) * i / 8, y1 + (y2 - y1) * i / 8);
            queue(false, x2, y2);
        } else if (sscanf(a, "%d,%d", &x1, &y1) == 2) {
            if (x1 < 0) { s_now += (int64_t)(-x1) * 1000; queue(false, 0, 0); }   /* wait */
            else { queue(true, x1, y1); queue(false, x1, y1); }
        } else { fprintf(stderr, "bad point %s\n", a); exit(2); }
    }
    const int i = s_qi++;
    *x = s_qx[i];
    *y = s_qy[i];
    return s_qd[i];
}
void touch_swallow(void) {}
void touch_set_rotation(int quarter_turns) { (void)quarter_turns; }

/* NVS: nothing saved, nothing loaded */
void save_init(void) {}
/* SCREENTEST_DEAL=dragons: three dragon pungs on top, for the meld row
 * and the limit-hand celebration. SCREENTEST_DEAL=played: a game forty
 * entries in (SCREENTEST_PLAYED=n for another number), for the rail. Anything else: a normal deal. */
bool save_load_game(um_game_t *g)
{
    const char *d = getenv("SCREENTEST_DEAL");
    if (d && !strcmp(d, "played")) {
        /* a long rail: the hint's sets, with a Discard 6 between, until
         * forty entries are logged */
        const int want = getenv("SCREENTEST_PLAYED") ? atoi(getenv("SCREENTEST_PLAYED")) : 40;
        um_new(g, 20261017u);
        for (int step = 0; step < 400 && g->log_n < want && !g->done; step++) {
            um_sel_t s = { 0, 0 };
            if (step % 2 == 0 && um_hint(g, &s)) { um_clear(g, s, NULL, NULL); continue; }
            for (int i = 0, n = 0; i < g->hand_len && n < UM_DISCARD_N; i++) if (g->hand[i] != UM_EMPTY) { s.hand |= 1u << i; n++; }
            if (!um_discard_six(g, s) && !um_add_six(g)) break;
        }
        return true;
    }
    if (!d || strcmp(d, "dragons")) return false;
    uint8_t wall[UM_TOTAL], cnt[UM_KINDS] = { 0 };
    int n = 0;
    for (int k = 31; k <= 33; k++) for (int i = 0; i < 3; i++) { wall[n++] = (uint8_t)k; cnt[k]++; }
    for (int k = 0; k < UM_KINDS; k++) while (cnt[k] < 4) { wall[n++] = (uint8_t)k; cnt[k]++; }
    um_new_from_wall(g, wall);
    return true;
}
void save_game(const um_game_t *g) { (void)g; }
/* SCREENTEST_ROT=0..3 picks the orientation, landscape (1) otherwise;
 * SCREENTEST_LANG=0..2 the language (i18n_lang_t) and SCREENTEST_THEME=1
 * the dark theme. SCREENTEST_DSET=1 turns Discard set on. */
void save_load_prefs(save_prefs_t *p)
{
    const char *r = getenv("SCREENTEST_ROT"), *l = getenv("SCREENTEST_LANG"), *t = getenv("SCREENTEST_THEME");
    p->best_points = INT32_MIN; p->best_penalty = INT32_MIN; p->auto_sort = 0;
    p->discard_set = (uint8_t)(getenv("SCREENTEST_DSET") ? 1 : 0);
    p->rotation = (uint8_t)(r ? atoi(r) & 3 : 1);
    p->lang = (uint8_t)(l ? atoi(l) : 0);
    p->theme = (uint8_t)(t ? atoi(t) & 1 : 0);
}
void save_prefs(const save_prefs_t *p) { (void)p; }

extern int ui_qr_icon_pct;

int main(int argc, char **argv)
{
    if (getenv("SCREENTEST_QR_ICON")) ui_qr_icon_pct = atoi(getenv("SCREENTEST_QR_ICON"));   /* the QR icon's height, % */
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
