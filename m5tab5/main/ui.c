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

#include <ctype.h>
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

#include "save.h"
#include "tiledraw.h"
#include "umcore.h"

static const char *TAG = "ui";

/* The web version's light theme. */
#define C_BG       RGB(0xe3, 0xe9, 0xe1)
#define C_SURFACE  RGB(0xd3, 0xdd, 0xd0)
#define C_RAISED   RGB(0xf1, 0xf4, 0xee)
#define C_INK      RGB(0x1d, 0x28, 0x26)
#define C_MUTED    RGB(0x5b, 0x6b, 0x66)
#define C_LINE     RGB(0xa9, 0xb8, 0xad)
#define C_ACCENT   RGB(0x2c, 0x4b, 0x84)
#define C_ON_ACC   RGB(0xf4, 0xf6, 0xfb)
#define C_WARN     RGB(0x9a, 0x3b, 0x1c)
#define C_GREEN    RGB(0x1e, 0x72, 0x47)

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

static const char *const HON_NAMES[7] = { "East wind", "South wind", "West wind", "North wind",
                                          "Red dragon", "Green dragon", "White dragon" };

static void kind_name(int k, char *out, size_t n)
{
    if (k >= 27) { snprintf(out, n, "%s", HON_NAMES[k - 27]); return; }
    static const char *const S1[3] = { "Character", "Dot", "Bamboo" };
    static const char *const SN[3] = { "Characters", "Dots", "Bamboo" };
    const int r = k % 9 + 1, s = k / 9;
    snprintf(out, n, "%d %s", r, r == 1 ? S1[s] : SN[s]);
}

/* "big four winds" -> "Big Four Winds" for the named hands */
static void set_name(um_set_t t, char *out, size_t n)
{
    snprintf(out, n, "%s", um_set_key(t));
    if (um_set_is_limit(t) || um_set_is_classic(t)) {
        bool start = true;
        for (char *p = out; *p; p++) { if (start && islower((unsigned char)*p)) *p = (char)toupper((unsigned char)*p); start = *p == ' '; }
    }
}

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
typedef struct { int x, y, w; char label[24]; bool enabled, primary, cost, armed; } button_t;
static button_t s_btn[B_COUNT];

static void layout_buttons(void)
{
    static const char *const base[B_COUNT] = { "Clear", "Meld", "Add 6", "Discard 6", "Hint", "Sort", "Auto", "Undo", "New deal" };
    const int64_t now = esp_timer_get_time();
    for (int b = 0; b < B_COUNT; b++) snprintf(s_btn[b].label, sizeof s_btn[b].label, "%s", base[b]);
    if (um_is_kong_upgrade(&s_g, s_sel)) snprintf(s_btn[B_MELD].label, sizeof s_btn[B_MELD].label, "Make kong");
    snprintf(s_btn[B_AUTO].label, sizeof s_btn[B_AUTO].label, "Auto: %s", s_prefs.auto_sort ? "on" : "off");
    if (now < s_armed_until) snprintf(s_btn[B_NEW].label, sizeof s_btn[B_NEW].label, "Tap again");

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
    const int pad = L.land ? 28 : 20, gap = L.land ? 10 : 8, min_w = L.land ? 104 : 96;
    const int split = L.btn_rows == 1 ? B_COUNT : B_SORT;
    for (int b = 0; b < B_COUNT; b++) {
        int w = gfx_text_w(s_btn[b].label, TXT) + pad + (s_btn[b].cost ? gfx_text_w(" -6", TXT) : 0);
        s_btn[b].w = w < min_w ? min_w : w;
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

/* Turn: top right, cycling the four orientations. */
static void turn_rect(int *x, int *y, int *w, int *h)
{
    *w = gfx_text_w("Turn", TXT) + 28;
    *h = 40;
    *x = gfx_w() - M_X - *w;
    *y = 8;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */

static void text(int x, int y, const char *s, uint16_t c) { gfx_draw_text(x, y, s, TXT, gfx_w() - x - M_X, c); }

static void draw_top(void)
{
    char pts[32], pen[32], sub[96], bp[16], bq[16], pv[16];
    text(M_X, TOP_Y, "Unprofitable Mahjong", C_INK);
    int tx, ty, tw, th;
    turn_rect(&tx, &ty, &tw, &th);
    gfx_fill_rect(tx, ty, tw, th, C_LINE);
    gfx_fill_rect(tx + 2, ty + 2, tw - 4, th - 4, C_RAISED);
    gfx_draw_text(tx + 14, ty + (th - GFX_GLYPH_H(TXT)) / 2, "Turn", TXT, tw, C_INK);
    /* the two scores, their bests and the piles: right-aligned beside Turn
     * in landscape, on their own lines under the title in portrait */
    pts_text(um_penalty(&s_g), pv, sizeof pv);
    snprintf(pts, sizeof pts, "Points %d", (int)s_g.points);
    snprintf(pen, sizeof pen, "   Penalty %s", pv);
    if (s_prefs.best_points == INT32_MIN) snprintf(bp, sizeof bp, "none"); else snprintf(bp, sizeof bp, "%d", (int)s_prefs.best_points);
    if (s_prefs.best_penalty == INT32_MIN) snprintf(bq, sizeof bq, "none"); else pts_text(s_prefs.best_penalty, bq, sizeof bq);
    snprintf(sub, sizeof sub, "best %s / %s    wall %d    discards %d", bp, bq, UM_TOTAL - s_g.wall_pos, s_g.disc_n);
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
    if (!s_g.log_n) { gfx_draw_text(x, L.rail_y + 10, "Cleared sets and penalties will line up here.", TXT, gfx_w() - 2 * M_X, C_MUTED); return; }
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
    snprintf(s, sizeof s, "Melds %d/%d", s_g.meld_n, UM_MAX_MELDS);
    gfx_draw_text(L.meld_label_x, L.meld_label_y, s, TXT, 170, C_MUTED);
    if (!s_g.meld_n) {
        /* beside the label in landscape, under it in portrait */
        const int ex = L.land ? L.meld_x0 : M_X, ey = L.land ? L.meld_label_y : L.meld_y + 16;
        gfx_draw_para(ex, ey, "Set sets aside here to build toward a bigger hand.", TXT, gfx_w() - ex - M_X,
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
    char l1[256] = "", l2[256] = "", tmp[48];
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
            snprintf(l1, sizeof l1, "All 136 cleared: %d points, %s penalty.", (int)s_g.points, pen);
            snprintf(l2, sizeof l2, "%s", um_penalty(&s_g) == 0 ? "A clean sweep with no shortcuts." : "Every tile cleared. Try the next deal without shortcuts.");
            c2 = C_GREEN;
        } else {
            snprintf(l1, sizeof l1, "Game over: %d points, %s penalty.", (int)s_g.points, pen);
            snprintf(l2, sizeof l2, "%d tiles left with no set to make, at -%d each. Undo, or start a new deal.", um_in_hand(&s_g), UM_LEFT_COST);
            c2 = C_WARN;
        }
    } else if (!n) {
        const uint8_t lone = um_lone_honors(&s_g);
        if (esp_timer_get_time() < s_hint_until) snprintf(l2, sizeof l2, "The glowing tiles make a set.");
        else if (lone) {
            size_t at = (size_t)snprintf(l2, sizeof l2, "A lone ");
            bool first = true;
            for (int h = 0; h < 7; h++) if (lone & (1u << h)) {
                at += (size_t)snprintf(l2 + at, sizeof l2 - at, "%s%s", first ? "" : " and a lone ", HON_NAMES[h]);
                first = false;
            }
            snprintf(l2 + at, sizeof l2 - at, " can never be cleared now.%s", s_undo_n ? " Undo to fix it." : "");
            c2 = C_WARN;
        } else if (!any_set && pool && um_in_hand(&s_g)) {
            snprintf(l2, sizeof l2, "No sets showing. %s", um_can_add(&s_g) ? "Add 6 or discard 6 to see more tiles." : "Discard 6 to see more tiles.");
            c2 = C_WARN;
        } else if (um_in_hand(&s_g) > UM_HAND && !um_can_add(&s_g) && pool) {
            snprintf(l2, sizeof l2, "%d tiles out. You can add 6 again once you're back to %d.", um_in_hand(&s_g), UM_MAX_HAND - UM_ADD_N);
        } else {
            snprintf(l2, sizeof l2, "Tap tiles to make a set. Bigger sets score more. Select 6 to discard.");
        }
    } else {
        uint8_t sk[UM_MAX_SEL];
        memcpy(sk, k, (size_t)n);
        for (int i = 1; i < n; i++) { uint8_t v = sk[i]; int j = i - 1; while (j >= 0 && sk[j] > v) { sk[j + 1] = sk[j]; j--; } sk[j + 1] = v; }
        size_t at = 0;
        for (int i = 0; i < n && at < sizeof l1 - 32; i++) {
            kind_name(sk[i], tmp, sizeof tmp);
            at += (size_t)snprintf(l1 + at, sizeof l1 - at, "%s%s", i ? ", " : "", tmp);
        }
        const um_set_t t = um_classify(k, n);
        c2 = C_INK;
        if (t != UM_NONE) {
            char nm[48];
            set_name(t, nm, sizeof nm);
            const bool named = um_set_is_limit(t) || um_set_is_classic(t);
            const int v = um_set_value(t, k, n);
            const bool bonus = um_set_has_bonus(t, k, n);
            snprintf(l2, sizeof l2, "Makes %s%s for +%d%s", named ? "" : "a ", nm, v,
                     bonus ? ", its mahjong sets plus 1 for clearing them together." : um_set_is_poker(t) ? ", poker only." : ".");
        } else if (n == UM_DISCARD_N) {
            snprintf(l2, sizeof l2, "Discard these 6 for -%d.", UM_DISCARD_COST);
        } else if (n == 5) {
            snprintf(l2, sizeof l2, "Not a set. Pick one more to discard 6.");
            c2 = C_WARN;
        } else if (n == 1) {
            snprintf(l2, sizeof l2, "Pick more tiles.");
        } else {
            snprintf(l2, sizeof l2, "Not a set yet.");
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
    const char *kick = "LIMIT HAND";
    gfx_draw_text((gfx_w() - gfx_text_w(kick, TXT)) / 2, y0 - 50, kick, TXT, gfx_w(), RGB(0xb5, 0x30, 0x2a));
    gfx_blit(0, gfx_h());
    /* tiles one at a time, each band blitted as it lands */
    for (int i = 0; i < n; i++) {
        const int x = x0 + (i % per_row) * pitch, y = y0 + (i / per_row) * (sz->h + 16);
        tiledraw_tile(sz, k[i], x, y, TILEDRAW_EDGE_M);
        gfx_blit(y, y + sz->h + TILEDRAW_EDGE_M);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    char nm[48], ps[16];
    set_name(t, nm, sizeof nm);
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
    const char *tap = "Tap to continue";
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
    touch_swallow();    /* the finger that tapped Turn is on a different control now */
}

/* 90 -> 180 -> 270 -> 0: landscape and portrait alternate */
static void turn(void)
{
    s_prefs.rotation = (uint8_t)((s_prefs.rotation + 1) % 4);
    save_prefs(&s_prefs);
    apply_rotation(s_prefs.rotation);
}

static void tap(int x, int y)
{
    /* buttons */
    {
        int tx, ty, tw, th;
        turn_rect(&tx, &ty, &tw, &th);
        if (x >= tx && x < tx + tw && y >= ty && y < ty + th) { turn(); return; }
    }
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
