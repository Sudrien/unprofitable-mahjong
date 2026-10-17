/*
 * ui.c -- see ui.h.
 *
 * The whole screen is redrawn into the shadow buffer after anything
 * changes and blitted in one go. It is a slow game: a move is a tap, and
 * a full-screen blit at the rotated angle is tens of milliseconds, which
 * nobody waiting to think about their next move will see. Band blits are
 * an optimisation for later, if a board run says they are needed.
 *
 * The text and behaviour follow the web version's render() in
 * ../../index.html; so does the order things are checked in.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gfx.h"
#include "touch.h"

#include "i18n.h"
#include "save.h"
#include "tiledraw.h"
#include "umcore.h"

static const char *TAG = "ui";

/* The web version's two themes. The tiles keep their ivory faces in
 * both, as they do on the web: a tile is a thing on the table, not
 * part of the page. */
typedef struct { uint16_t bg, surface, raised, ink, muted, line, accent, on_acc, warn, green, kick; } theme_t;
static const theme_t THEMES[2] = {
    {   /* light */
        RGB(0xe3, 0xe9, 0xe1), RGB(0xd3, 0xdd, 0xd0), RGB(0xf1, 0xf4, 0xee), RGB(0x1d, 0x28, 0x26),
        RGB(0x5b, 0x6b, 0x66), RGB(0xa9, 0xb8, 0xad), RGB(0x2c, 0x4b, 0x84), RGB(0xf4, 0xf6, 0xfb),
        RGB(0x9a, 0x3b, 0x1c), RGB(0x1e, 0x72, 0x47), RGB(0xb5, 0x30, 0x2a),
    },
    {   /* dark */
        RGB(0x11, 0x1a, 0x1b), RGB(0x19, 0x29, 0x2a), RGB(0x20, 0x32, 0x34), RGB(0xe2, 0xea, 0xe5),
        RGB(0x93, 0xa6, 0xa0), RGB(0x36, 0x50, 0x4e), RGB(0x93, 0xb2, 0xec), RGB(0x0f, 0x1a, 0x2c),
        RGB(0xf0, 0xa0, 0x7f), RGB(0x7f, 0xc8, 0x9b), RGB(0xec, 0x6f, 0x63),
    },
};
static const theme_t *P = &THEMES[0];
#define C_BG       (P->bg)
#define C_SURFACE  (P->surface)
#define C_RAISED   (P->raised)
#define C_INK      (P->ink)
#define C_MUTED    (P->muted)
#define C_LINE     (P->line)
#define C_ACCENT   (P->accent)
#define C_ON_ACC   (P->on_acc)
#define C_WARN     (P->warn)
#define C_GREEN    (P->green)
#define C_KICK     (P->kick)

/* Layout, in logical pixels. The fixed parts: */
#define M_X        (24)
#define TOP_Y      (12)
#define HAND_LIFT  (12)
#define BTN_H      (88)
#define TXT        (2)          /* text scale: Ark12 at 24 px */

/* The parts that move with the orientation, filled by relayout() from
 * gfx_w()/gfx_h() whenever the screen turns. Landscape is 1280x720 with
 * the hand 9 (or 12) across and one row of buttons; portrait is 720x1280
 * with the hand 6 across and the buttons in two rows. */
typedef struct {
    bool land;
    int rail_y, table_y;
    int meld_label_x, meld_label_y;  /* "Melds 2/4" */
    int meld_x0, meld_y;             /* the melds themselves */
    int hand_y, cols, cols_wide;     /* cols_wide: after Add 6 */
    int status_y, status_rows;       /* rows the second status line may wrap to */
    int btn_y, btn_rows;
} layout_t;
static layout_t L;

#define HINT_US    (2400 * 1000)
#define ARM_US     (2500 * 1000)
#define UNDO_N     (64)

static um_game_t s_g;
static um_sel_t s_sel;
static uint32_t s_hint_mask;
static int64_t s_hint_until, s_armed_until;
static save_prefs_t s_prefs;
static um_game_t *s_undo;      /* UNDO_N snapshots, a ring, in PSRAM */
static int s_undo_head, s_undo_n;

/* ------------------------------------------------------------------ */
/* Words                                                               */

/* Every tile by name, one key each: a number and a suit are said in a
 * different order, and with different numerals, in each language. */
static const char *const KIND_NAMES[UM_KINDS] = {
    N_("1 Character"), N_("2 Characters"), N_("3 Characters"), N_("4 Characters"), N_("5 Characters"),
    N_("6 Characters"), N_("7 Characters"), N_("8 Characters"), N_("9 Characters"),
    N_("1 Dot"), N_("2 Dots"), N_("3 Dots"), N_("4 Dots"), N_("5 Dots"),
    N_("6 Dots"), N_("7 Dots"), N_("8 Dots"), N_("9 Dots"),
    N_("1 Bamboo"), N_("2 Bamboo"), N_("3 Bamboo"), N_("4 Bamboo"), N_("5 Bamboo"),
    N_("6 Bamboo"), N_("7 Bamboo"), N_("8 Bamboo"), N_("9 Bamboo"),
    N_("East wind"), N_("South wind"), N_("West wind"), N_("North wind"),
    N_("Red dragon"), N_("Green dragon"), N_("White dragon"),
};
static const char *kind_name(int k) { return _(KIND_NAMES[k]); }

/* The web version's names: English as it shows them (the named hands in
 * Title Case), Japanese in riichi terms, Chinese in the MCR's. */
static const char *const SET_NAMES[UM_SET_COUNT] = {
    [UM_NONE] = "",
    [UM_PAIR] = N_("pair"), [UM_CHOW] = N_("chow"), [UM_PUNG] = N_("pung"), [UM_KONG] = N_("kong"),
    [UM_TWO_PAIR] = N_("two pair"), [UM_FLUSH] = N_("flush"), [UM_STRAIGHT] = N_("straight"),
    [UM_FULL_HOUSE] = N_("full house"), [UM_ROYAL_FLUSH] = N_("royal flush"),
    [UM_RUN4] = N_("run of 4"), [UM_RUN5] = N_("run of 5"), [UM_RUN6] = N_("run of 6"),
    [UM_RUN7] = N_("run of 7"), [UM_RUN8] = N_("run of 8"), [UM_FULL_STRAIGHT] = N_("full straight"),
    [UM_DRAGON_SET] = N_("dragon set"), [UM_WIND_SET] = N_("wind set"),
    [UM_MIXED_DOUBLE_CHOW] = N_("mixed double chow"), [UM_PURE_DOUBLE_CHOW] = N_("pure double chow"),
    [UM_MIXED_TRIPLE_CHOW] = N_("mixed triple chow"), [UM_PURE_TRIPLE_CHOW] = N_("pure triple chow"),
    [UM_SEVEN_PAIRS] = N_("Seven Pairs"), [UM_BIG_THREE_DRAGONS] = N_("Big Three Dragons"),
    [UM_LITTLE_FOUR_WINDS] = N_("Little Four Winds"), [UM_ALL_HONORS] = N_("All Honors"),
    [UM_ALL_TERMINALS] = N_("All Terminals"), [UM_BIG_FOUR_WINDS] = N_("Big Four Winds"),
    [UM_THIRTEEN_ORPHANS] = N_("Thirteen Orphans"), [UM_NINE_GATES] = N_("Nine Gates"),
    [UM_LITTLE_THREE_DRAGONS] = N_("Little Three Dragons"), [UM_TRIPLE_PUNG] = N_("Triple Pung"),
    [UM_MIXED_STRAIGHT] = N_("Mixed Straight"), [UM_THREE_KONGS] = N_("Three Kongs"),
    [UM_ALL_SIMPLES] = N_("All Simples"), [UM_OUTSIDE_HAND] = N_("Outside Hand"),
    [UM_HALF_FLUSH] = N_("Half Flush"), [UM_ALL_PUNGS] = N_("All Pungs"), [UM_FULL_FLUSH] = N_("Full Flush"),
    [UM_ALL_GREEN] = N_("All Green"), [UM_FOUR_KONGS] = N_("Four Kongs"),
};
static const char *set_name(um_set_t t) { return (t > UM_NONE && t < UM_SET_COUNT) ? _(SET_NAMES[t]) : ""; }

static void pts_text(int v, char *out, size_t n)
{
    if (v == 0) snprintf(out, n, "0");
    else snprintf(out, n, "-%d", v < 0 ? -v : v);
}

/* ------------------------------------------------------------------ */
/* Undo                                                                */

static void undo_push(void)
{
    if (!s_undo) return;
    memcpy(&s_undo[s_undo_head], &s_g, sizeof s_g);
    s_undo_head = (s_undo_head + 1) % UNDO_N;
    if (s_undo_n < UNDO_N) s_undo_n++;
}

static bool undo_pop(void)
{
    if (!s_undo || !s_undo_n) return false;
    s_undo_head = (s_undo_head + UNDO_N - 1) % UNDO_N;
    s_undo_n--;
    memcpy(&s_g, &s_undo[s_undo_head], sizeof s_g);
    return true;
}

/* ------------------------------------------------------------------ */
/* Layout: where each tile and button is, shared by draw and hit test  */

static void relayout(void)
{
    L.land = gfx_w() > gfx_h();
    if (L.land) {
        L.rail_y = 58; L.table_y = 104;
        L.meld_label_x = M_X; L.meld_label_y = 140;
        L.meld_x0 = 200; L.meld_y = 116;
        L.hand_y = 222; L.cols = 9; L.cols_wide = 12;
        L.status_y = 512; L.status_rows = 1;
        L.btn_y = 616; L.btn_rows = 1;
    } else {
        L.rail_y = 104; L.table_y = 152;
        L.meld_label_x = M_X; L.meld_label_y = 164;
        L.meld_x0 = M_X; L.meld_y = 196;
        L.hand_y = 304; L.cols = 6; L.cols_wide = 6;
        L.status_y = 892; L.status_rows = 2;   /* moved up under a short hand: status_top() */
        L.btn_y = 1068; L.btn_rows = 2;
    }
}

static int hand_cols(void) { return s_g.hand_len > UM_HAND ? L.cols_wide : L.cols; }

/* In portrait the hand is three rows, or four after Add 6; the status
 * line sits under whichever it is rather than under the fourth row's
 * empty space. Landscape always has room for exactly two. */
static int status_top(void)
{
    if (L.land) return L.status_y;
    const int rows = (s_g.hand_len + L.cols - 1) / L.cols;
    return L.hand_y + rows * (tilefaces_L.h + TILEDRAW_EDGE_L + 12) + 8;
}
static int hand_pitch(void) { return hand_cols() == 12 ? 102 : 110; }

static void hand_xy(int i, int *x, int *y)
{
    const int cols = hand_cols(), pitch = hand_pitch();
    const int span = cols * pitch - (pitch - tilefaces_L.w);
    *x = (gfx_w() - span) / 2 + (i % cols) * pitch;
    *y = L.hand_y + (i / cols) * (tilefaces_L.h + TILEDRAW_EDGE_L + 12);
}

/* The meld row uses the medium faces when they fit, the small ones when
 * four long melds would not. */
static const tilefaces_size_t *meld_size(int *edge, int *gap)
{
    int tiles = 0;
    for (int m = 0; m < s_g.meld_n; m++) tiles += s_g.meld_len[m];
    const int room = gfx_w() - L.meld_x0 - M_X;
    if (tiles * (tilefaces_M.w + 2) + s_g.meld_n * 18 <= room) { *edge = TILEDRAW_EDGE_M; *gap = 18; return &tilefaces_M; }
    *edge = TILEDRAW_EDGE_S; *gap = 12;
    return &tilefaces_S;
}

static int meld_x(int m)
{
    int edge, gap;
    const tilefaces_size_t *sz = meld_size(&edge, &gap);
    int x = L.meld_x0;
    for (int i = 0; i < m; i++) x += s_g.meld_len[i] * (sz->w + 2) + gap;
    return x;
}

enum { B_CLEAR, B_MELD, B_ADD, B_DISCARD, B_HINT, B_SORT, B_AUTO, B_UNDO, B_NEW, B_COUNT };
typedef struct { int x, y, w; const char *label; bool enabled, primary, cost, armed; } button_t;
static button_t s_btn[B_COUNT];

static void layout_buttons(void)
{
    static const char *const base[B_COUNT] = { N_("Clear"), N_("Meld"), N_("Add 6"), N_("Discard 6"), N_("Hint"),
                                               N_("Sort"), N_("Auto: off"), N_("Undo"), N_("New deal") };
    const int64_t now = esp_timer_get_time();
    for (int b = 0; b < B_COUNT; b++) s_btn[b].label = _(base[b]);
    if (um_is_kong_upgrade(&s_g, s_sel)) s_btn[B_MELD].label = _("Make kong");
    if (s_prefs.auto_sort) s_btn[B_AUTO].label = _("Auto: on");
    if (now < s_armed_until) s_btn[B_NEW].label = _("Tap again");

    uint8_t k[UM_MAX_SEL];
    const int n = um_sel_kinds(&s_g, s_sel, k);
    s_btn[B_CLEAR].enabled = n >= 2 && um_classify(k, n) != UM_NONE;
    s_btn[B_MELD].enabled = um_can_meld(&s_g, s_sel);
    s_btn[B_ADD].enabled = um_can_add(&s_g);
    s_btn[B_DISCARD].enabled = um_can_discard(&s_g, s_sel);
    s_btn[B_HINT].enabled = um_in_hand(&s_g) > 0;
    s_btn[B_SORT].enabled = um_in_hand(&s_g) > 0;
    s_btn[B_AUTO].enabled = true;
    s_btn[B_UNDO].enabled = s_undo_n > 0;
    s_btn[B_NEW].enabled = true;
    s_btn[B_CLEAR].primary = true;
    s_btn[B_ADD].cost = s_btn[B_DISCARD].cost = true;
    s_btn[B_NEW].armed = now < s_armed_until;

    /* one row in landscape; two in portrait, Clear..Hint over Sort..New */
    const int gap = L.land ? 10 : 8;
    const int split = L.btn_rows == 1 ? B_COUNT : B_SORT;
    /* As roomy as the widest row allows: Japanese labels are longer than
     * English ones, so the padding gives way before a row overflows. */
    for (int pad = L.land ? 28 : 20, min_w = L.land ? 104 : 96; ; pad -= 4, min_w -= 8) {
        for (int b = 0; b < B_COUNT; b++) {
            int w = gfx_text_w(s_btn[b].label, TXT) + pad + (s_btn[b].cost ? gfx_text_w(" -6", TXT) : 0);
            s_btn[b].w = w < min_w ? min_w : w;
        }
        int widest = 0;
        for (int row = 0, first = 0; first < B_COUNT; row++) {
            const int last = row == 0 ? split : B_COUNT;
            int total = 0;
            for (int b = first; b < last; b++) total += s_btn[b].w + (b > first ? gap : 0);
            if (total > widest) widest = total;
            first = last;
        }
        if (widest <= gfx_w() - 2 * M_X || pad <= 8) break;
    }
    for (int row = 0, first = 0; first < B_COUNT; row++) {
        const int last = row == 0 ? split : B_COUNT;
        int total = 0;
        for (int b = first; b < last; b++) total += s_btn[b].w + (b > first ? gap : 0);
        int x = (gfx_w() - total) / 2;
        for (int b = first; b < last; b++) {
            s_btn[b].x = x;
            s_btn[b].y = L.btn_y + row * (BTN_H + 12);
            x += s_btn[b].w + gap;
        }
        first = last;
    }
}

/* The gear: top right, opening the settings. */
static void gear_rect(int *x, int *y, int *w, int *h)
{
    *w = 60;
    *h = 48;
    *x = gfx_w() - M_X - *w;
    *y = 4;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */

static void text(int x, int y, const char *s, uint16_t c) { gfx_draw_text(x, y, s, TXT, gfx_w() - x - M_X, c); }

static void box(int x, int y, int w, int h, uint16_t border, uint16_t fill)
{
    gfx_fill_rect(x, y, w, h, border);
    gfx_fill_rect(x + 2, y + 2, w - 4, h - 4, fill);
}

/* Ark12 has no U+2699, so the gear is drawn: eight teeth, each a
 * trapezoid on the rim, a disc over their roots and a hole. Sines of
 * 0..315 degrees in steps of 45, times 1024, so no libm. */
static void draw_gear(int cx, int cy, uint16_t ink, uint16_t hole)
{
    static const int S[8] = { 0, 724, 1024, 724, 0, -724, -1024, -724 };
    const int r_in = 12, r_out = 19, half_in = 5, half_out = 4;
    for (int i = 0; i < 8; i++) {
        const int s = S[i], c = S[(i + 2) % 8];     /* sin, cos */
        /* along the spoke (c, s), across it (-s, c) */
        int xy[8];
        const int pts[4][2] = { { r_in, -half_in }, { r_out, -half_out }, { r_out, half_out }, { r_in, half_in } };
        for (int j = 0; j < 4; j++) {
            const int a = pts[j][0], b = pts[j][1];
            xy[j * 2] = cx + (a * c - b * s) / 1024;
            xy[j * 2 + 1] = cy + (a * s + b * c) / 1024;
        }
        gfx_fill_poly(xy, 4, ink);
    }
    gfx_fill_circle(cx, cy, 14, ink);
    gfx_fill_circle(cx, cy, 6, hole);
}

static void draw_top(void)
{
    char pts[48], pen[48], sub[160], bp[24], bq[24], pv[16];
    text(M_X, TOP_Y, _("Unprofitable Mahjong"), C_INK);
    int tx, ty, tw, th;
    gear_rect(&tx, &ty, &tw, &th);
    box(tx, ty, tw, th, C_LINE, C_RAISED);
    draw_gear(tx + tw / 2, ty + th / 2, C_INK, C_RAISED);
    /* the two scores, their bests and the piles: right-aligned beside the
     * gear in landscape, on their own lines under the title in portrait */
    pts_text(um_penalty(&s_g), pv, sizeof pv);
    snprintf(pts, sizeof pts, _("Points %d"), (int)s_g.points);
    snprintf(pen, sizeof pen, "   ");
    snprintf(pen + 3, sizeof pen - 3, _("Penalty %s"), pv);
    if (s_prefs.best_points == INT32_MIN) snprintf(bp, sizeof bp, "%s", _("none")); else snprintf(bp, sizeof bp, "%d", (int)s_prefs.best_points);
    if (s_prefs.best_penalty == INT32_MIN) snprintf(bq, sizeof bq, "%s", _("none")); else pts_text(s_prefs.best_penalty, bq, sizeof bq);
    snprintf(sub, sizeof sub, _("best %s / %s    wall %d    discards %d"), bp, bq, UM_TOTAL - s_g.wall_pos, s_g.disc_n);
    const int wa = gfx_text_w(pts, TXT), wb = gfx_text_w(pen, TXT), ws = gfx_text_w(sub, 1);
    int x, y, sx, sy;
    if (L.land) {
        x = tx - 20 - wa - wb; y = TOP_Y;
        sx = tx - 20 - ws; sy = TOP_Y + GFX_GLYPH_H(TXT) + 2;
    } else {
        x = M_X; y = 56;
        sx = M_X; sy = y + GFX_GLYPH_H(TXT) + 6;
    }
    gfx_draw_text(x, y, pts, TXT, wa + 2, s_g.points > 0 ? C_GREEN : C_INK);
    gfx_draw_text(x + wa, y, pen, TXT, wb + 2, um_penalty(&s_g) < 0 ? C_WARN : C_INK);
    gfx_draw_text(sx, sy, sub, 1, ws + 2, C_MUTED);
}

static void draw_rail(void)
{
    int x = M_X;
    if (!s_g.log_n) { gfx_draw_text(x, L.rail_y + 10, _("Cleared sets and penalties will line up here."), TXT, gfx_w() - 2 * M_X, C_MUTED); return; }
    const int keep = s_g.log_n < UM_LOG_N ? s_g.log_n : UM_LOG_N;
    for (int j = 0; j < keep; j++) {
        const um_log_t *e = &s_g.log[(s_g.log_n - 1 - j) % UM_LOG_N];
        char v[16];
        int w;
        if (e->t == UM_LOG_ADD) {
            snprintf(v, sizeof v, "+%d", e->n);
            w = gfx_text_w(v, TXT) + 12;
            if (x + w > gfx_w() - M_X) break;
            gfx_fill_rect(x, L.rail_y, w, 2, C_WARN); gfx_fill_rect(x, L.rail_y + 34, w, 2, C_WARN);
            gfx_fill_rect(x, L.rail_y, 2, 36, C_WARN); gfx_fill_rect(x + w - 2, L.rail_y, 2, 36, C_WARN);
            gfx_draw_text(x + 6, L.rail_y + 6, v, TXT, w, C_WARN);
        } else {
            w = e->n * (tilefaces_S.w + 1);
            if (x + w > gfx_w() - M_X) break;
            for (int i = 0; i < e->n; i++) tiledraw_tile(&tilefaces_S, e->k[i], x + i * (tilefaces_S.w + 1), L.rail_y, TILEDRAW_EDGE_S);
            if (e->t == UM_LOG_DISCARD) gfx_fill_rect(x, L.rail_y + tilefaces_S.h + 4, w, 2, C_WARN);
        }
        /* what it was worth, beside it */
        char pv[16];
        if (e->t == UM_LOG_ADD || e->t == UM_LOG_DISCARD) pts_text(e->v, pv, sizeof pv);
        else snprintf(pv, sizeof pv, "+%d", e->v);
        gfx_draw_text(x + w + 4, L.rail_y + 12, pv, 1, 40, e->v < 0 ? C_WARN : C_MUTED);
        x += w + 4 + gfx_text_w(pv, 1) + 16;
    }
}

static void draw_melds(void)
{
    char s[32];
    snprintf(s, sizeof s, _("Melds %d/%d"), s_g.meld_n, UM_MAX_MELDS);
    gfx_draw_text(L.meld_label_x, L.meld_label_y, s, TXT, 170, C_MUTED);
    if (!s_g.meld_n) {
        /* beside the label in landscape, under it in portrait */
        const int ex = L.land ? L.meld_x0 : M_X, ey = L.land ? L.meld_label_y : L.meld_y + 16;
        gfx_draw_para(ex, ey, _("Set sets aside here to build toward a bigger hand."), TXT, gfx_w() - ex - M_X,
                      GFX_GLYPH_H(TXT) + 6, 2, C_MUTED);
        return;
    }
    int edge, gap;
    const tilefaces_size_t *sz = meld_size(&edge, &gap);
    for (int m = 0; m < s_g.meld_n; m++) {
        const int x = meld_x(m);
        const bool sel = s_sel.melds & (1u << m);
        const int y = L.meld_y + (sel ? 0 : 8);
        for (int i = 0; i < s_g.meld_len[m]; i++) tiledraw_tile(sz, s_g.melds[m][i], x + i * (sz->w + 2), y, edge);
        if (sel) {
            const int w = s_g.meld_len[m] * (sz->w + 2) - 2;
            gfx_fill_rect(x - 3, y - 3, w + 6, 3, C_ACCENT);
            gfx_fill_rect(x - 3, y + sz->h + edge, w + 6, 3, C_ACCENT);
            gfx_fill_rect(x - 3, y, 3, sz->h + edge, C_ACCENT);
            gfx_fill_rect(x + w, y, 3, sz->h + edge, C_ACCENT);
        }
    }
}

static void draw_hand(void)
{
    const bool hinting = esp_timer_get_time() < s_hint_until;
    for (int i = 0; i < s_g.hand_len; i++) {
        int x, y;
        hand_xy(i, &x, &y);
        if (s_g.hand[i] == UM_EMPTY) {
            gfx_fill_rect(x + 6, y + 6, tilefaces_L.w - 12, tilefaces_L.h - 12, C_BG);
            continue;
        }
        const bool sel = s_sel.hand & (1u << i), hint = hinting && (s_hint_mask & (1u << i));
        if (sel) y -= HAND_LIFT;
        tiledraw_tile(&tilefaces_L, s_g.hand[i], x, y, TILEDRAW_EDGE_L);
        if (sel) tiledraw_ring(&tilefaces_L, x, y, TILEDRAW_EDGE_L, 3, C_ACCENT);
        else if (hint) tiledraw_ring(&tilefaces_L, x, y, TILEDRAW_EDGE_L, 4, C_GREEN);
    }
}

static void draw_status(void)
{
    char l1[320] = "", l2[320] = "", tmp[160];
    uint16_t c2 = C_MUTED;
    uint8_t k[UM_MAX_SEL];
    const int n = um_sel_kinds(&s_g, s_sel, k);
    const bool pool = um_pool(&s_g) > 0;
    um_sel_t any;
    const bool any_set = um_find_set(&s_g, false, &any);

    if (s_g.done) {
        /* the end of the game, said in the status area */
        char pen[16];
        pts_text(um_penalty(&s_g), pen, sizeof pen);
        if (!um_in_hand(&s_g)) {
            snprintf(l1, sizeof l1, _("All 136 cleared: %d points, %s penalty."), (int)s_g.points, pen);
            snprintf(l2, sizeof l2, "%s", um_penalty(&s_g) == 0 ? _("A clean sweep with no shortcuts.")
                                                                 : _("Every tile cleared. Try the next deal without shortcuts."));
            c2 = C_GREEN;
        } else {
            snprintf(l1, sizeof l1, _("Game over: %d points, %s penalty."), (int)s_g.points, pen);
            snprintf(l2, sizeof l2, _("%d tiles left with no set to make, at -%d each. Undo, or start a new deal."),
                     um_in_hand(&s_g), UM_LEFT_COST);
            c2 = C_WARN;
        }
    } else if (!n) {
        const uint8_t lone = um_lone_honors(&s_g);
        if (esp_timer_get_time() < s_hint_until) snprintf(l2, sizeof l2, "%s", _("The glowing tiles make a set."));
        else if (lone) {
            /* "A lone East wind and a lone Red dragon ...": the names
             * joined first, then put into the sentence whole */
            size_t at = 0;
            tmp[0] = 0;
            for (int h = 0; h < 7; h++) if (lone & (1u << h)) {
                at += (size_t)snprintf(tmp + at, sizeof tmp - at, "%s%s", at ? _(" and a lone ") : "", kind_name(27 + h));
                if (at >= sizeof tmp) break;
            }
            at = (size_t)snprintf(l2, sizeof l2, _("A lone %s can never be cleared now."), tmp);
            if (s_undo_n && at < sizeof l2) snprintf(l2 + at, sizeof l2 - at, "%s", _(" Undo to fix it."));
            c2 = C_WARN;
        } else if (!any_set && pool && um_in_hand(&s_g)) {
            snprintf(l2, sizeof l2, "%s", um_can_add(&s_g) ? _("No sets showing. Add 6 or discard 6 to see more tiles.")
                                                           : _("No sets showing. Discard 6 to see more tiles."));
            c2 = C_WARN;
        } else if (um_in_hand(&s_g) > UM_HAND && !um_can_add(&s_g) && pool) {
            snprintf(l2, sizeof l2, _("%d tiles out. You can add 6 again once you're back to %d."), um_in_hand(&s_g), UM_MAX_HAND - UM_ADD_N);
        } else {
            snprintf(l2, sizeof l2, "%s", _("Tap tiles to make a set. Bigger sets score more. Select 6 to discard."));
        }
    } else {
        uint8_t sk[UM_MAX_SEL];
        memcpy(sk, k, (size_t)n);
        for (int i = 1; i < n; i++) { uint8_t v = sk[i]; int j = i - 1; while (j >= 0 && sk[j] > v) { sk[j + 1] = sk[j]; j--; } sk[j + 1] = v; }
        size_t at = 0;
        for (int i = 0; i < n && at < sizeof l1 - 40; i++)
            at += (size_t)snprintf(l1 + at, sizeof l1 - at, "%s%s", i ? _(", ") : "", kind_name(sk[i]));
        const um_set_t t = um_classify(k, n);
        c2 = C_INK;
        if (t != UM_NONE) {
            /* the named hands take no article in English; the others do */
            const bool named = um_set_is_limit(t) || um_set_is_classic(t);
            const int v = um_set_value(t, k, n);
            const bool bonus = um_set_has_bonus(t, k, n), poker = !bonus && um_set_is_poker(t);
            const char *f = named
                ? (bonus ? _("Makes %s for +%d, its mahjong sets plus 1 for clearing them together.")
                   : poker ? _("Makes %s for +%d, poker only.") : _("Makes %s for +%d."))
                : (bonus ? _("Makes a %s for +%d, its mahjong sets plus 1 for clearing them together.")
                   : poker ? _("Makes a %s for +%d, poker only.") : _("Makes a %s for +%d."));
            snprintf(l2, sizeof l2, f, set_name(t), v);
        } else if (n == UM_DISCARD_N) {
            snprintf(l2, sizeof l2, _("Discard these 6 for -%d."), UM_DISCARD_COST);
        } else if (n == 5) {
            snprintf(l2, sizeof l2, "%s", _("Not a set. Pick one more to discard 6."));
            c2 = C_WARN;
        } else if (n == 1) {
            snprintf(l2, sizeof l2, "%s", _("Pick more tiles."));
        } else {
            snprintf(l2, sizeof l2, "%s", _("Not a set yet."));
            c2 = C_WARN;
        }
    }
    const int w = gfx_w() - 2 * M_X;
    const int sy = status_top();
    if (l1[0]) gfx_draw_text(M_X, sy + 8, l1, TXT, w, C_INK);
    if (l2[0]) gfx_draw_para(M_X, sy + 44, l2, TXT, w, GFX_GLYPH_H(TXT) + 6, L.status_rows, c2);
}

static void draw_buttons(void)
{
    layout_buttons();
    for (int b = 0; b < B_COUNT; b++) {
        const button_t *bt = &s_btn[b];
        const bool on = bt->enabled;
        const uint16_t fill = bt->primary && on ? C_ACCENT : C_RAISED;
        const uint16_t border = bt->armed ? C_WARN : bt->primary && on ? C_ACCENT : C_LINE;
        const uint16_t fg = !on ? C_LINE : bt->armed ? C_WARN : bt->primary ? C_ON_ACC : C_INK;
        gfx_fill_rect(bt->x, bt->y, bt->w, BTN_H, border);
        gfx_fill_rect(bt->x + 2, bt->y + 2, bt->w - 4, BTN_H - 4, fill);
        const int tw = gfx_text_w(bt->label, TXT) + (bt->cost ? gfx_text_w(" -6", TXT) : 0);
        const int tx = bt->x + (bt->w - tw) / 2, ty = bt->y + (BTN_H - GFX_GLYPH_H(TXT)) / 2;
        gfx_draw_text(tx, ty, bt->label, TXT, bt->w, fg);
        if (bt->cost) gfx_draw_text(tx + gfx_text_w(bt->label, TXT), ty, " -6", TXT, bt->w, on ? C_WARN : C_LINE);
    }
}

/* ------------------------------------------------------------------ */
/* Settings: the gear's panel                                          */

/* Three rows of choices and Done. Each choice takes effect as it is
 * tapped -- the panel turns with the screen, recolours with the theme
 * and changes language under the finger -- so there is nothing to
 * confirm and Done only closes it. */
enum { G_ROT, G_THEME, G_LANG, G_DONE, G_COUNT };
#define CHIP_H     (72)
#define CHIP_MAX   (12)
typedef struct { int x, y, w, group, value; const char *label; } chip_t;
static bool s_menu;
static chip_t s_chip[CHIP_MAX];
static int s_chip_n, s_head_y[G_DONE], s_pan_x, s_pan_y, s_pan_w, s_pan_h, s_title_y;

/* landscape first, as the board starts; each followed by its flip */
static const uint8_t ROT_ORDER[4] = { 1, 0, 3, 2 };
static const char *const ROT_NAMES[4] = { N_("Landscape"), N_("Portrait"), N_("Landscape, flipped"), N_("Portrait, flipped") };
static const char *const THEME_NAMES[2] = { N_("Light"), N_("Dark") };
static const uint8_t LANG_ORDER[I18N_LANG_COUNT] = { I18N_EN, I18N_JA, I18N_ZH_CN };
static const char *const HEADS[G_DONE] = { N_("Orientation"), N_("Theme"), N_("Language") };

static bool chip_on(const chip_t *c)
{
    switch (c->group) {
    case G_ROT:   return c->value == s_prefs.rotation;
    case G_THEME: return c->value == s_prefs.theme;
    case G_LANG:  return c->value == (int)i18n_lang();
    default:      return false;
    }
}

static void layout_menu(void)
{
    const int pw = gfx_w() - 2 * M_X < 960 ? gfx_w() - 2 * M_X : 960;
    const int ix = 32, iw = pw - 64, gap = 12;
    int y = 24;
    s_title_y = y;
    y += GFX_GLYPH_H(TXT) + 28;
    s_chip_n = 0;
    for (int g = 0; g < G_DONE; g++) {
        s_head_y[g] = y;
        y += GFX_GLYPH_H(TXT) + 12;
        const int n = g == G_ROT ? 4 : g == G_THEME ? 2 : I18N_LANG_COUNT;
        int cx = ix;
        for (int i = 0; i < n; i++) {
            chip_t *c = &s_chip[s_chip_n++];
            c->group = g;
            c->value = g == G_ROT ? ROT_ORDER[i] : g == G_THEME ? i : LANG_ORDER[i];
            /* the languages by their own names, which no reader needs translated */
            c->label = g == G_ROT ? _(ROT_NAMES[i]) : g == G_THEME ? _(THEME_NAMES[i]) : i18n_lang_name((i18n_lang_t)c->value);
            int w = gfx_text_w(c->label, TXT) + 32;    /* 32: the four turns fit one row in landscape */
            if (w < 128) w = 128;
            if (cx > ix && cx + w > ix + iw) { cx = ix; y += CHIP_H + gap; }
            c->x = cx; c->y = y; c->w = w;
            cx += w + gap;
        }
        y += CHIP_H + 32;
    }
    chip_t *d = &s_chip[s_chip_n++];
    d->group = G_DONE; d->value = 0; d->label = _("Done");
    d->w = gfx_text_w(d->label, TXT) + 80;
    if (d->w < 180) d->w = 180;
    d->x = ix + iw - d->w; d->y = y;
    y += CHIP_H + 24;
    /* across the middle, from a fixed top: a choice that rewraps the
     * rows (a language, a turn) moves Done, but not what is above it */
    s_pan_w = pw; s_pan_h = y;
    s_pan_x = (gfx_w() - pw) / 2;
    s_pan_y = L.land ? 28 : 160;
    s_title_y += s_pan_y;
    for (int g = 0; g < G_DONE; g++) s_head_y[g] += s_pan_y;
    for (int i = 0; i < s_chip_n; i++) { s_chip[i].x += s_pan_x; s_chip[i].y += s_pan_y; }
}

static void draw_menu(void)
{
    layout_menu();
    box(s_pan_x - 2, s_pan_y - 2, s_pan_w + 4, s_pan_h + 4, C_LINE, C_RAISED);
    gfx_draw_text(s_pan_x + 32, s_title_y, _("Settings"), TXT, s_pan_w - 64, C_INK);
    gfx_fill_rect(s_pan_x + 32, s_title_y + GFX_GLYPH_H(TXT) + 12, s_pan_w - 64, 2, C_LINE);
    for (int g = 0; g < G_DONE; g++)
        gfx_draw_text(s_pan_x + 32, s_head_y[g], _(HEADS[g]), TXT, s_pan_w - 64, C_MUTED);
    for (int i = 0; i < s_chip_n; i++) {
        const chip_t *c = &s_chip[i];
        const bool on = chip_on(c), primary = c->group == G_DONE;
        box(c->x, c->y, c->w, CHIP_H, on || primary ? C_ACCENT : C_LINE, on || primary ? C_ACCENT : C_BG);
        gfx_draw_text(c->x + (c->w - gfx_text_w(c->label, TXT)) / 2, c->y + (CHIP_H - GFX_GLYPH_H(TXT)) / 2,
                      c->label, TXT, c->w, on || primary ? C_ON_ACC : C_INK);
    }
}

static void redraw(void)
{
    gfx_fill_rect(0, 0, gfx_w(), gfx_h(), C_BG);
    gfx_fill_rect(M_X / 2, L.table_y, gfx_w() - M_X, L.btn_y - L.table_y - 12, C_SURFACE);
    draw_top();
    draw_rail();
    draw_melds();
    draw_hand();
    draw_status();
    draw_buttons();
    if (s_menu) draw_menu();
    gfx_blit(0, gfx_h());
}

/* ------------------------------------------------------------------ */
/* The limit-hand celebration                                          */

static void celebrate(um_set_t t, const uint8_t *kinds, int n, int v)
{
    /* in tile order, as the web version shows them */
    uint8_t k[UM_MAX_SEL];
    memcpy(k, kinds, (size_t)n);
    for (int i = 1; i < n; i++) { uint8_t x = k[i]; int j = i - 1; while (j >= 0 && k[j] > x) { k[j + 1] = k[j]; j--; } k[j + 1] = x; }

    const tilefaces_size_t *sz = &tilefaces_M;
    const int pitch = sz->w + 4;
    const int per_row = (gfx_w() - 2 * M_X) / pitch;
    const int rows = (n + per_row - 1) / per_row;
    const int row_w = (n < per_row ? n : per_row) * pitch - 4;
    const int x0 = (gfx_w() - row_w) / 2;
    const int y0 = L.land ? 170 : 420;     /* about the middle of either screen */

    gfx_fill_rect(0, 0, gfx_w(), gfx_h(), C_SURFACE);
    const char *kick = _("LIMIT HAND");
    gfx_draw_text((gfx_w() - gfx_text_w(kick, TXT)) / 2, y0 - 50, kick, TXT, gfx_w(), C_KICK);
    gfx_blit(0, gfx_h());
    /* tiles one at a time, each band blitted as it lands */
    for (int i = 0; i < n; i++) {
        const int x = x0 + (i % per_row) * pitch, y = y0 + (i / per_row) * (sz->h + 16);
        tiledraw_tile(sz, k[i], x, y, TILEDRAW_EDGE_M);
        gfx_blit(y, y + sz->h + TILEDRAW_EDGE_M);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    char ps[16];
    const char *nm = set_name(t);
    const int ny = y0 + rows * (sz->h + 16) + 30;
    const int ns = gfx_text_w(nm, 5) <= gfx_w() - 2 * M_X ? 5 : 4;   /* portrait is 720 wide */
    gfx_draw_text((gfx_w() - gfx_text_w(nm, ns)) / 2, ny, nm, ns, gfx_w(), C_INK);
    gfx_blit(ny, ny + GFX_GLYPH_H(ns));
    /* the points count up, eased out, as on the web */
    const int py = ny + GFX_GLYPH_H(ns) + 24;
    for (int f = 0; f <= 30; f++) {
        const float p = (float)f / 30.0f, e = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
        snprintf(ps, sizeof ps, "+%d", (int)(v * e + 0.5f));
        gfx_fill_rect(0, py, gfx_w(), GFX_GLYPH_H(4), C_SURFACE);
        gfx_draw_text((gfx_w() - gfx_text_w(ps, 4)) / 2, py, ps, 4, gfx_w(), C_GREEN);
        gfx_blit(py, py + GFX_GLYPH_H(4));
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    const char *tap = _("Tap to continue");
    gfx_draw_text((gfx_w() - gfx_text_w(tap, TXT)) / 2, py + 80, tap, TXT, gfx_w(), C_MUTED);
    gfx_blit(py + 80, py + 80 + GFX_GLYPH_H(TXT));
    /* until a tap, or 4 s */
    touch_swallow();
    int x, y;
    for (int ms = 0; ms < 4200; ms += 20) {
        if (touch_get(&x, &y)) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    touch_swallow();
}

/* ------------------------------------------------------------------ */
/* Moves                                                               */

static void after_move(bool was_done)
{
    if (s_prefs.auto_sort && !s_g.done) um_sort(&s_g);
    if (!was_done && s_g.done) {
        if (s_prefs.best_penalty == INT32_MIN || um_penalty(&s_g) > s_prefs.best_penalty) s_prefs.best_penalty = um_penalty(&s_g);
        if (s_prefs.best_points == INT32_MIN || s_g.points > s_prefs.best_points) s_prefs.best_points = s_g.points;
        save_prefs(&s_prefs);
    }
    s_sel.hand = 0;
    s_sel.melds = 0;
    s_hint_until = 0;
    save_game(&s_g);
}

static void new_deal(void)
{
    um_new(&s_g, esp_random());
    s_undo_n = 0;
    after_move(false);
}

static void press(int b)
{
    if (!s_btn[b].enabled) return;
    const bool was_done = s_g.done;
    switch (b) {
    case B_CLEAR: {
        uint8_t k[UM_MAX_SEL];
        const int n = um_sel_kinds(&s_g, s_sel, k);
        um_set_t t;
        int v;
        undo_push();
        if (!um_clear(&s_g, s_sel, &t, &v)) { undo_pop(); return; }
        if (um_set_is_limit(t)) celebrate(t, k, n, v);
        break;
    }
    case B_MELD:
        undo_push();
        if (!um_meld(&s_g, s_sel)) { undo_pop(); return; }
        break;
    case B_ADD:
        if (!um_add_six(&s_g)) return;
        s_undo_n = 0;       /* adding is final */
        break;
    case B_DISCARD:
        if (!um_discard_six(&s_g, s_sel)) return;
        s_undo_n = 0;       /* and so is discarding */
        break;
    case B_HINT: {
        um_sel_t h;
        s_sel.hand = 0; s_sel.melds = 0;
        s_hint_mask = um_hint(&s_g, &h) ? h.hand : 0;
        s_hint_until = esp_timer_get_time() + HINT_US;
        return;
    }
    case B_SORT:
        undo_push();
        um_sort(&s_g);
        break;
    case B_AUTO:
        s_prefs.auto_sort = !s_prefs.auto_sort;
        save_prefs(&s_prefs);
        if (s_prefs.auto_sort) { undo_push(); um_sort(&s_g); save_game(&s_g); }
        return;
    case B_UNDO:
        if (!undo_pop()) return;
        s_sel.hand = 0; s_sel.melds = 0;
        save_game(&s_g);
        return;
    case B_NEW:
        if (esp_timer_get_time() < s_armed_until) { s_armed_until = 0; new_deal(); }
        else s_armed_until = esp_timer_get_time() + ARM_US;
        return;
    default:
        return;
    }
    after_move(was_done);
}

/* The picture and the touch coordinates turn together, or every tap
 * lands somewhere other than where it was drawn. */
static void apply_rotation(int r)
{
    gfx_set_rotation(r);
    touch_set_rotation(r);
    relayout();
    touch_swallow();    /* the finger that chose it is over a different control now */
}

static void apply_theme(int t) { P = &THEMES[t == 1 ? 1 : 0]; }

static void menu_tap(int x, int y)
{
    layout_menu();
    if (x < s_pan_x || x >= s_pan_x + s_pan_w || y < s_pan_y || y >= s_pan_y + s_pan_h) { s_menu = false; return; }
    for (int i = 0; i < s_chip_n; i++) {
        const chip_t *c = &s_chip[i];
        if (x < c->x || x >= c->x + c->w || y < c->y || y >= c->y + CHIP_H) continue;
        switch (c->group) {
        case G_ROT:
            if (c->value == s_prefs.rotation) return;
            s_prefs.rotation = (uint8_t)c->value;
            apply_rotation(c->value);
            break;
        case G_THEME:
            s_prefs.theme = (uint8_t)c->value;
            apply_theme(c->value);
            break;
        case G_LANG:
            s_prefs.lang = (uint8_t)c->value;
            i18n_set_lang((i18n_lang_t)c->value);
            break;
        default:
            s_menu = false;
            return;
        }
        save_prefs(&s_prefs);
        return;
    }
}

static void tap(int x, int y)
{
    if (s_menu) { menu_tap(x, y); return; }
    {
        int gx, gy, gw, gh;
        gear_rect(&gx, &gy, &gw, &gh);
        if (x >= gx && x < gx + gw && y >= gy && y < gy + gh) { s_menu = true; s_hint_until = 0; s_armed_until = 0; return; }
    }
    /* buttons */
    if (y >= L.btn_y) {
        for (int b = 0; b < B_COUNT; b++)
            if (x >= s_btn[b].x && x < s_btn[b].x + s_btn[b].w && y >= s_btn[b].y && y < s_btn[b].y + BTN_H) { press(b); return; }
        return;
    }
    if (s_g.done) return;
    s_hint_until = 0;
    /* hand tiles (a lifted tile is hit where it is drawn, or where it was) */
    for (int i = 0; i < s_g.hand_len; i++) {
        if (s_g.hand[i] == UM_EMPTY) continue;
        int tx, ty;
        hand_xy(i, &tx, &ty);
        if (x >= tx && x < tx + tilefaces_L.w && y >= ty - HAND_LIFT && y < ty + tilefaces_L.h + TILEDRAW_EDGE_L) {
            const uint32_t bit = 1u << i;
            if (s_sel.hand & bit) s_sel.hand &= ~bit;
            else if (um_sel_count(&s_g, s_sel) < UM_MAX_SEL) s_sel.hand |= bit;
            return;
        }
    }
    /* melds, whole */
    if (s_g.meld_n && y >= L.meld_y && y < L.meld_y + tilefaces_M.h + TILEDRAW_EDGE_M + 12) {
        int edge, gap;
        const tilefaces_size_t *sz = meld_size(&edge, &gap);
        for (int m = 0; m < s_g.meld_n; m++) {
            const int mx = meld_x(m), mw = s_g.meld_len[m] * (sz->w + 2);
            if (x >= mx && x < mx + mw) {
                const uint8_t bit = (uint8_t)(1u << m);
                if (s_sel.melds & bit) s_sel.melds &= (uint8_t)~bit;
                else if (um_sel_count(&s_g, s_sel) + s_g.meld_len[m] <= UM_MAX_SEL) s_sel.melds |= bit;
                return;
            }
        }
    }
}

/* ------------------------------------------------------------------ */

void ui_run(void)
{
    s_undo = heap_caps_malloc(sizeof(um_game_t) * UNDO_N, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_undo) ESP_LOGW(TAG, "no PSRAM for undo; playing without it");
    save_load_prefs(&s_prefs);
    i18n_set_lang((i18n_lang_t)s_prefs.lang);
    apply_theme(s_prefs.theme);
    apply_rotation(s_prefs.rotation);
    if (save_load_game(&s_g)) ESP_LOGI(TAG, "resumed a saved game");
    else new_deal();

    bool was_down = false, dirty = true;
    int64_t hint_seen = 0, armed_seen = 0;
    for (;;) {
        int x = 0, y = 0;
        const bool down = touch_get(&x, &y);
        if (down && !was_down) { tap(x, y); dirty = true; }
        was_down = down;
        /* the hint glow and the armed New deal both time out on their own */
        const int64_t now = esp_timer_get_time();
        const bool hint_on = now < s_hint_until, armed_on = now < s_armed_until;
        if (hint_on != (hint_seen != 0) || armed_on != (armed_seen != 0)) dirty = true;
        hint_seen = hint_on; armed_seen = armed_on;
        if (dirty) { redraw(); dirty = false; }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}
