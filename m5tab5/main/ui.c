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
 * ../../web/index.html; so does the order things are checked in.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui.h"

#include <stdarg.h>
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
#include "guide.h"
#include "qrcodegen.h"
#include "tiledraw.h"
#include "libversions.h"     /* generated: cmake/libversions.cmake */
#include "umach.h"
#include "umcore.h"
#include "umreplay.h"

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
#define TOP_Y      (8)
#define HAND_LIFT  (12)
/* Buttons: 80 px tall in landscape, where they sit in one row; 64 in
 * portrait, where three rows of them would otherwise crowd the status
 * line. L is the layout, below; this only ever expands after it. */
#define BTN_H      (L.land ? 80 : 64)
/* Text scales. The panel is about 294 pixels to the inch, so Ark12 at
 * 24 px (scale 2) is under 6 pt: too small to read across a table.
 * Everything to read is 36 px (scale 3, about 9 pt); 24 px is kept for
 * the line of bests and piles and the rail's values, which are glanced
 * at, not read. */
#define TXT        (3)
#define SMALL      (2)
#define LINE       (GFX_GLYPH_H(TXT) + 4)

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
    int status_y;
    int btn_y;
} layout_t;
static layout_t L;

#define HINT_US    (2400 * 1000)
#define ARM_US     (2500 * 1000)
#define UNDO_N     (64)

static um_game_t s_g;
static um_sel_t s_sel;
static uint32_t s_hint_mask;
static int64_t s_hint_until, s_armed_until;
/* A set's First or Full: a banner over the rail until `until`, then gone.
 * Full screen is for limit hands, wins and the flood. */
#define BANNER_US (2500000)
static struct {
    int64_t until;
    um_set_t t;
    bool full;
    uint8_t n, k[UM_MAX_SEL];
    char kick[96];
    const char *name;   /* a secret's, untranslated; NULL: the set's */
} s_ban;
static save_prefs_t s_prefs;
static um_ach_t s_ach;          /* the achievements, saved whole whenever one changes */
static um_game_t *s_undo;      /* UNDO_N snapshots, a ring, in PSRAM */
static int s_undo_head, s_undo_n;
static bool s_guide;                /* the list of valid hands is open */
static int s_guide_sel = -1;
static int s_gach_sel = -1;    /* an achievement card chosen in the Sets list, for its note */

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

/* The achievements' names and notes, the web version's WINS and
 * FLOOD_NAME, in umach.h's order. */
static const char *const WIN_NAMES[UM_WIN_N] = { N_("Win with no poker hands"), N_("Win with no pairs"), N_("Win with every poker hand") };
static const char *const WIN_NOTES[UM_WIN_N] = {
    N_("All 136 cleared without two pair, a flush, a straight, a full house or a royal flush."),
    N_("All 136 cleared and no set held a pair: no pairs, two pair or full houses, and no hand with a pair in it, like All Honors or Seven Pairs."),
    N_("All 136 cleared with two pair, a flush, a straight and a full house among the sets. A royal flush counts as the flush."),
};
/* The secrets, the web version's SECRETS: listed only once earned. */
static const char *const SECRET_NAMES[UM_SECRET_N] = { N_("A Place for Everything"), N_("Wait Wait Wait"), N_("Intentional Game Design") };
static const char *const SECRET_NOTES[UM_SECRET_N] = {
    N_("Finished a game with no tiles left."),
    N_("Sorted the hand with tiles chosen, and lost the choice."),
    N_("Tried to grow a melded pair into a pung. Only a pung grows."),
};
#define FLOOD_NAME N_("The Yellow River Floods Again")
#define FLOOD_NOTE N_("Clear every set at least once. The river rises with each one.")
#define CAPITAL_NAME N_("The Capital is Gone")
#define CAPITAL_NOTE N_("Make every set full: each cleared in 3 of 5 games in a row.")
#define HIGH_NAME N_("High Score")
#define HIGH_NOTE N_("Score 500 points in one game.")

/* A heading in English, as the web version's title(): each word's first
 * letter up, "of" left alone -- Full House, Run of 4. Other languages
 * have no case and are copied as they are. */
static const char *heading(const char *in, char *out, size_t cap, bool en)
{
    if (!en) return in;
    size_t i = 0;
    for (; in[i] && i + 1 < cap; i++) {
        const bool start = !i || in[i - 1] == ' ';
        out[i] = (start && in[i] >= 'a' && in[i] <= 'z' && strncmp(in + i, "of ", 3)) ? (char)(in[i] - 32) : in[i];
    }
    out[i] = 0;
    return out;
}

/* printf onto the end of out, never past cap */
static void put(char *out, size_t cap, size_t *at, const char *fmt, ...)
{
    if (*at + 1 >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(out + *at, cap - *at, fmt, ap);
    va_end(ap);
    if (w > 0) *at += (size_t)w;
    if (*at >= cap) *at = cap - 1;
}

/* The selection, sorted, said briefly, as the web version's tilesIn():
 * a suit's tiles share one suit name and a repeated tile is named once
 * with its count. "1 ×3, 2, 3 Characters · North wind ×3"; in Japanese
 * "1112萬、北×3" and in Chinese "一一一二万、北风×3", as the tiles are
 * written there. The shape differs by language, not just the words, so
 * this asks which language it is rather than going through a key. */
static void tiles_text(const uint8_t *k, int n, char *out, size_t cap)
{
    static const char *const EN_SUIT[3] = { "Characters", "Dots", "Bamboo" };
    static const char *const JA_SUIT[3] = { "萬", "筒", "索" };
    static const char *const ZH_SUIT[3] = { "万", "筒", "条" };
    static const char *const ZH_NUM[9] = { "一", "二", "三", "四", "五", "六", "七", "八", "九" };
    const i18n_lang_t lang = i18n_lang();
    const bool en = lang != I18N_JA && lang != I18N_ZH_CN;
    size_t at = 0;
    out[0] = 0;
    for (int i = 0; i < n; ) {
        /* a group: one honor's tiles, or one suit's */
        const int suit = um_is_honor(k[i]) ? -1 : k[i] / 9;
        int j = i, kinds = 0;
        while (j < n && (suit < 0 ? k[j] == k[i] : (!um_is_honor(k[j]) && k[j] / 9 == suit))) {
            if (j == i || k[j] != k[j - 1]) kinds++;
            j++;
        }
        if (i) put(out, cap, &at, "%s", en ? " · " : "、");
        if (suit < 0 || (en && kinds == 1)) {                    /* "North wind ×3", "9 Dots ×3" */
            put(out, cap, &at, "%s", kind_name(k[i]));
            if (j - i > 1) put(out, cap, &at, en ? " ×%d" : "×%d", j - i);
        } else if (en) {                                         /* "1 ×3, 2, 3 Characters" */
            for (int x = i; x < j; ) {
                int y = x;
                while (y < j && k[y] == k[x]) y++;
                put(out, cap, &at, "%s%d", x > i ? ", " : "", k[x] % 9 + 1);
                if (y - x > 1) put(out, cap, &at, " ×%d", y - x);
                x = y;
            }
            put(out, cap, &at, " %s", EN_SUIT[suit]);
        } else {                                                 /* "1112萬", "一一一二万" */
            for (int x = i; x < j; x++) {
                if (lang == I18N_JA) put(out, cap, &at, "%d", k[x] % 9 + 1);
                else put(out, cap, &at, "%s", ZH_NUM[k[x] % 9]);
            }
            put(out, cap, &at, "%s", lang == I18N_JA ? JA_SUIT[suit] : ZH_SUIT[suit]);
        }
        i = j;
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
        L.rail_y = 76; L.table_y = 120;
        L.meld_label_x = M_X; L.meld_label_y = 138;
        L.meld_x0 = 240; L.meld_y = 128;
        L.hand_y = 222; L.cols = 9; L.cols_wide = 12;
        L.status_y = 506;
        L.btn_y = 630;
    } else {
        L.rail_y = 134; L.table_y = 184;
        L.meld_label_x = M_X; L.meld_label_y = 196;
        L.meld_x0 = M_X; L.meld_y = 244;
        L.hand_y = 330; L.cols = 6; L.cols_wide = 6;
        L.status_y = 0;                       /* under the hand: status_top() */
        L.btn_y = 1280 - 8 - 3 * BTN_H - 2 * 8;
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
    return L.hand_y + rows * (tilefaces_L.h + TILEDRAW_EDGE_L + 12) - 4;
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

/* Auto-sort and New deal live in the gear's panel: they are settings
 * and a rare act, and at 36 px text the other seven fill a row. */
enum { B_CLEAR, B_MELD, B_ADD, B_DISCARD, B_HINT, B_SORT, B_UNDO, B_COUNT };
typedef struct { int x, y, w; const char *label; bool enabled, primary, cost; char cost_txt[12]; } button_t;
static button_t s_btn[B_COUNT];

/* One row in landscape. Three in portrait: what a move is made with,
 * what costs, and the helpers. */
static const uint8_t ORDER_LAND[B_COUNT] = { B_CLEAR, B_MELD, B_ADD, B_DISCARD, B_HINT, B_SORT, B_UNDO };
static const uint8_t ROWS_LAND[] = { 7, 0 };
static const uint8_t ORDER_PORT[B_COUNT] = { B_CLEAR, B_MELD, B_UNDO, B_ADD, B_DISCARD, B_HINT, B_SORT };
static const uint8_t ROWS_PORT[] = { 3, 2, 2, 0 };

/* What Discard would do with the selection: 1 Discard 6, 2 Discard set
 * (only with the setting on), 0 nothing. Six tiles always go as Discard
 * 6, which is never dearer: every six-tile set is worth more than 6. */
static int discard_kind(void)
{
    if (um_sel_count(&s_g, s_sel) == UM_DISCARD_N) return um_can_discard(&s_g, s_sel) ? 1 : 0;
    return s_prefs.discard_set && um_can_discard_set(&s_g, s_sel) ? 2 : 0;
}

static void layout_buttons(void)
{
    static const char *const base[B_COUNT] = { N_("Clear"), N_("Meld"), N_("Add 6"), N_("Discard 6"), N_("Hint"),
                                               N_("Sort"), N_("Undo") };
    for (int b = 0; b < B_COUNT; b++) s_btn[b].label = _(base[b]);
    if (um_is_kong_upgrade(&s_g, s_sel)) s_btn[B_MELD].label = _("Make kong");

    uint8_t k[UM_MAX_SEL];
    const int n = um_sel_kinds(&s_g, s_sel, k);
    s_btn[B_CLEAR].enabled = n >= 2 && um_classify(k, n) != UM_NONE;
    s_btn[B_MELD].enabled = um_can_meld(&s_g, s_sel);
    s_btn[B_ADD].enabled = um_can_add(&s_g);
    /* Discard: six tiles go as Discard 6; with Discard set on, a set
     * goes for what it would score (the web version's discardKind) */
    const int dk = discard_kind();
    s_btn[B_DISCARD].enabled = dk != 0;
    if (dk == 2) {
        s_btn[B_DISCARD].label = _("Discard set");
        snprintf(s_btn[B_DISCARD].cost_txt, sizeof s_btn[B_DISCARD].cost_txt, " -%d", um_set_value(um_classify(k, n), k, n));
    } else {
        snprintf(s_btn[B_DISCARD].cost_txt, sizeof s_btn[B_DISCARD].cost_txt, " -%d", UM_DISCARD_COST);
    }
    snprintf(s_btn[B_ADD].cost_txt, sizeof s_btn[B_ADD].cost_txt, " -%d", UM_ADD_COST);
    s_btn[B_HINT].enabled = um_in_hand(&s_g) > 0;
    s_btn[B_SORT].enabled = um_in_hand(&s_g) > 0;
    s_btn[B_UNDO].enabled = s_undo_n > 0;
    s_btn[B_CLEAR].primary = true;
    s_btn[B_ADD].cost = s_btn[B_DISCARD].cost = true;

    const uint8_t *order = L.land ? ORDER_LAND : ORDER_PORT, *rows = L.land ? ROWS_LAND : ROWS_PORT;
    const int gap = L.land ? 10 : 8;
    /* As roomy as the widest row allows: Japanese labels are longer than
     * English ones, so the padding gives way before a row overflows. */
    for (int pad = 40, min_w = 120; ; pad -= 4, min_w -= 8) {
        for (int b = 0; b < B_COUNT; b++) {
            int w = gfx_text_w(s_btn[b].label, TXT) + pad + (s_btn[b].cost ? gfx_text_w(s_btn[b].cost_txt, TXT) : 0);
            /* with Discard set on, Discard keeps the width of its longer
             * label, so the row does not shift as the selection changes */
            if (b == B_DISCARD && s_prefs.discard_set) {
                const int ws = gfx_text_w(_("Discard set"), TXT) + pad + gfx_text_w(" -16", TXT);
                const int w6 = gfx_text_w(_("Discard 6"), TXT) + pad + gfx_text_w(" -6", TXT);
                if (ws > w) w = ws;
                if (w6 > w) w = w6;
            }
            s_btn[b].w = w < min_w ? min_w : w;
        }
        int widest = 0;
        for (int r = 0, first = 0; rows[r]; first += rows[r], r++) {
            int total = 0;
            for (int i = first; i < first + rows[r]; i++) total += s_btn[order[i]].w + (i > first ? gap : 0);
            if (total > widest) widest = total;
        }
        if (widest <= gfx_w() - 2 * M_X || pad <= 8) break;
    }
    for (int r = 0, first = 0; rows[r]; first += rows[r], r++) {
        int total = 0;
        for (int i = first; i < first + rows[r]; i++) total += s_btn[order[i]].w + (i > first ? gap : 0);
        int x = (gfx_w() - total) / 2;
        for (int i = first; i < first + rows[r]; i++) {
            button_t *bt = &s_btn[order[i]];
            bt->x = x;
            bt->y = L.btn_y + r * (BTN_H + gap);
            x += bt->w + gap;
        }
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

/* Sets: the list of valid hands, just left of the gear. */
static void sets_rect(int *x, int *y, int *w, int *h)
{
    int gx, gy, gw, gh;
    gear_rect(&gx, &gy, &gw, &gh);
    *w = gfx_text_w(_("Sets"), TXT) + 32;
    *h = gh;
    *x = gx - 12 - *w;
    *y = gy;
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
    {
        int sx0, sy0, sw, sh;
        sets_rect(&sx0, &sy0, &sw, &sh);
        box(sx0, sy0, sw, sh, C_LINE, C_RAISED);
        gfx_draw_text(sx0 + (sw - gfx_text_w(_("Sets"), TXT)) / 2, sy0 + (sh - GFX_GLYPH_H(TXT)) / 2, _("Sets"), TXT, sw, C_INK);
        tx = sx0;       /* the scores sit left of both buttons */
    }
    /* the two scores, their bests and the piles: right-aligned beside the
     * gear in landscape, on their own lines under the title in portrait */
    pts_text(um_penalty(&s_g), pv, sizeof pv);
    snprintf(pts, sizeof pts, _("Points %d"), (int)s_g.points);
    snprintf(pen, sizeof pen, "   ");
    snprintf(pen + 3, sizeof pen - 3, _("Penalty %s"), pv);
    if (s_prefs.best_points == INT32_MIN) snprintf(bp, sizeof bp, "%s", _("none")); else snprintf(bp, sizeof bp, "%d", (int)s_prefs.best_points);
    if (s_prefs.best_penalty == INT32_MIN) snprintf(bq, sizeof bq, "%s", _("none")); else pts_text(s_prefs.best_penalty, bq, sizeof bq);
    snprintf(sub, sizeof sub, _("best %s / %s    wall %d    discards %d"), bp, bq, UM_TOTAL - s_g.wall_pos, s_g.disc_n);
    const int wa = gfx_text_w(pts, TXT), wb = gfx_text_w(pen, TXT), ws = gfx_text_w(sub, SMALL);
    int x, y, sx, sy;
    if (L.land) {
        x = tx - 20 - wa - wb; y = TOP_Y;
        sx = tx - 20 - ws; sy = TOP_Y + GFX_GLYPH_H(TXT) + 4;
    } else {
        x = M_X; y = 58;
        sx = M_X; sy = y + GFX_GLYPH_H(TXT) + 6;
    }
    gfx_draw_text(x, y, pts, TXT, wa + 2, s_g.points > 0 ? C_GREEN : C_INK);
    gfx_draw_text(x + wa, y, pen, TXT, wb + 2, um_penalty(&s_g) < 0 ? C_WARN : C_INK);
    gfx_draw_text(sx, sy, sub, SMALL, ws + 2, C_MUTED);
}

/* The rail: every clear and shortcut still kept (UM_LOG_N), newest at
 * the left. It is wider than the screen after a few moves, so a finger
 * drags it sideways (rail_touch, from ui_run's loop); a new entry brings
 * it back to the newest. */
static int s_rail_scroll, s_rail_max, s_rail_seen = -1;
static struct { bool active; int x0, scroll0; } s_rdrag;

static bool rail_hit(int x, int y)
{
    (void)x;
    return s_g.log_n && y >= L.rail_y - 8 && y < L.table_y;
}

static void draw_rail(void)
{
    if (!s_g.log_n) { gfx_draw_text(M_X, L.rail_y + 2, _("Clears and penalties go here."), TXT, gfx_w() - 2 * M_X, C_MUTED); return; }
    if (s_g.log_n != s_rail_seen) { s_rail_seen = s_g.log_n; s_rail_scroll = 0; }
    const int win_x = M_X, win_w = gfx_w() - 2 * M_X;
    /* one pass to measure, so the scroll can be held inside the list */
    const int keep = s_g.log_n < UM_LOG_N ? s_g.log_n : UM_LOG_N;
    int total = 0;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            s_rail_max = total > win_w ? total - win_w : 0;
            if (s_rail_scroll > s_rail_max) s_rail_scroll = s_rail_max;
            if (s_rail_scroll < 0) s_rail_scroll = 0;
        }
        int x = win_x - s_rail_scroll;
        for (int j = 0; j < keep; j++) {
            const um_log_t *e = &s_g.log[(s_g.log_n - 1 - j) % UM_LOG_N];
            char v[16], pv[16];
            int w;
            if (e->t == UM_LOG_ADD) { snprintf(v, sizeof v, "+%d", e->n); w = gfx_text_w(v, SMALL) + 12; }
            else w = e->n * (tilefaces_S.w + 1);
            if (e->t == UM_LOG_ADD || e->t == UM_LOG_DISCARD) pts_text(e->v, pv, sizeof pv);
            else snprintf(pv, sizeof pv, "+%d", e->v);
            const int step = w + 4 + gfx_text_w(pv, SMALL) + 18;
            if (pass == 0) { total += step; continue; }
            if (x + step > 0 && x < gfx_w()) {
                if (e->t == UM_LOG_ADD) {
                    gfx_fill_rect(x, L.rail_y, w, 2, C_WARN); gfx_fill_rect(x, L.rail_y + 34, w, 2, C_WARN);
                    gfx_fill_rect(x, L.rail_y, 2, 36, C_WARN); gfx_fill_rect(x + w - 2, L.rail_y, 2, 36, C_WARN);
                    gfx_draw_text_clipped(x + 6, L.rail_y + 6, win_x, win_w, v, SMALL, C_WARN);
                } else {
                    for (int i = 0; i < e->n; i++) tiledraw_tile(&tilefaces_S, e->k[i], x + i * (tilefaces_S.w + 1), L.rail_y, TILEDRAW_EDGE_S);
                    if (e->t == UM_LOG_DISCARD) gfx_fill_rect(x, L.rail_y + tilefaces_S.h + 4, w, 2, C_WARN);
                }
                /* what it was worth, beside it */
                gfx_draw_text_clipped(x + w + 4, L.rail_y + 6, win_x, win_w, pv, SMALL, e->v < 0 ? C_WARN : C_MUTED);
            }
            x += step;
        }
    }
    /* the margins, over whatever reached into them */
    gfx_fill_rect(0, L.rail_y - 2, win_x, 48, C_BG);
    gfx_fill_rect(win_x + win_w, L.rail_y - 2, gfx_w() - win_x - win_w, 48, C_BG);
    /* a hint that there is more off either end */
    if (s_rail_max > 0) {
        if (s_rail_scroll > 0) gfx_fill_rect(win_x - 10, L.rail_y + 4, 4, 28, C_LINE);
        if (s_rail_scroll < s_rail_max) gfx_fill_rect(win_x + win_w + 6, L.rail_y + 4, 4, 28, C_LINE);
    }
}

/* The finger on the rail, every pass of the loop while a drag is on.
 * Dragging left brings older entries in from the right. Returns whether
 * the screen needs drawing. */
static bool rail_touch(bool down, int x)
{
    if (!down) { s_rdrag.active = false; return false; }
    int to = s_rdrag.scroll0 + (s_rdrag.x0 - x);
    if (to > s_rail_max) to = s_rail_max;
    if (to < 0) to = 0;
    if (to == s_rail_scroll) return false;
    s_rail_scroll = to;
    return true;
}

static void draw_melds(void)
{
    char s[32];
    snprintf(s, sizeof s, _("Melds %d/%d"), s_g.meld_n, UM_MAX_MELDS);
    gfx_draw_text(L.meld_label_x, L.meld_label_y, s, TXT, L.land ? L.meld_x0 - M_X - 8 : gfx_w() - 2 * M_X, C_MUTED);
    if (!s_g.meld_n) {
        /* beside the label in landscape, under it in portrait */
        const int ex = L.land ? L.meld_x0 : M_X, ey = L.land ? L.meld_label_y : L.meld_y;
        gfx_draw_para(ex, ey, _("Set sets aside here to build a bigger hand."), TXT, gfx_w() - ex - M_X,
                      LINE, L.land ? 1 : 2, C_MUTED);
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
            snprintf(l2, sizeof l2, "%s", s_prefs.discard_set ? _("Tap tiles to make a set. Bigger sets score more. Select 6, or a set, to discard.")
                                                              : _("Tap tiles to make a set. Bigger sets score more. Select 6 to discard."));
        }
    } else {
        uint8_t sk[UM_MAX_SEL];
        memcpy(sk, k, (size_t)n);
        for (int i = 1; i < n; i++) { uint8_t v = sk[i]; int j = i - 1; while (j >= 0 && sk[j] > v) { sk[j + 1] = sk[j]; j--; } sk[j + 1] = v; }
        tiles_text(sk, n, l1, sizeof l1);
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
    /* The tiles chosen, then what they make, wrapped to as many rows as
     * reach the buttons. Landscape has room for the tile list only at
     * 24 px, on one line; it repeats what the lifted tiles already show.
     * Portrait gives the list two lines at 36 px, since 720 px holds
     * little of a big hand on one, and a third when a big one needs it
     * and the line of what it makes still fits under it. */
    const int w = gfx_w() - 2 * M_X;
    int y = status_top() + (L.land ? 6 : 2);
    if (l1[0] && L.land) { gfx_draw_text(M_X, y, l1, SMALL, w, C_INK); y += GFX_GLYPH_H(SMALL) + 8; }
    else if (l1[0]) {
        int r1 = gfx_para_rows(l1, TXT, w);
        if (r1 > 3) r1 = 3;
        if (r1 == 3 && y + 3 * LINE + 4 + GFX_GLYPH_H(TXT) > L.btn_y - 16) r1 = 2;
        y += gfx_draw_para(M_X, y, l1, TXT, w, LINE, r1, C_INK) * LINE + 4;
    }
    int rows = (L.btn_y - 16 - y - GFX_GLYPH_H(TXT)) / LINE + 1;
    if (rows > 3) rows = 3;
    if (rows < 1) rows = 1;
    if (l2[0]) gfx_draw_para(M_X, y, l2, TXT, w, LINE, rows, c2);
}

static void draw_buttons(void)
{
    layout_buttons();
    for (int b = 0; b < B_COUNT; b++) {
        const button_t *bt = &s_btn[b];
        const bool on = bt->enabled;
        const uint16_t fill = bt->primary && on ? C_ACCENT : C_RAISED;
        const uint16_t border = bt->primary && on ? C_ACCENT : C_LINE;
        const uint16_t fg = !on ? C_LINE : bt->primary ? C_ON_ACC : C_INK;
        gfx_fill_rect(bt->x, bt->y, bt->w, BTN_H, border);
        gfx_fill_rect(bt->x + 2, bt->y + 2, bt->w - 4, BTN_H - 4, fill);
        const int tw = gfx_text_w(bt->label, TXT) + (bt->cost ? gfx_text_w(bt->cost_txt, TXT) : 0);
        const int tx = bt->x + (bt->w - tw) / 2, ty = bt->y + (BTN_H - GFX_GLYPH_H(TXT)) / 2;
        gfx_draw_text(tx, ty, bt->label, TXT, bt->w, fg);
        if (bt->cost) gfx_draw_text(tx + gfx_text_w(bt->label, TXT), ty, bt->cost_txt, TXT, bt->w, on ? C_WARN : C_LINE);
    }
}

/* ------------------------------------------------------------------ */
/* Settings: the gear's panel                                          */

/* The whole screen: New deal and Share, then five rows of choices, Done,
 * and the licences. Each choice takes effect as it is tapped -- the
 * panel turns with the screen, recolours with the theme and changes
 * language under the finger -- so there is nothing to confirm and Done
 * only closes it. New deal asks for a second tap, as it did as a button.
 * In landscape the choices sit in two columns, since 720 px does not
 * hold them stacked at 36 px text. */
enum { G_ROT, G_THEME, G_LANG, G_AUTO, G_DSET, G_NEW, G_SHARE, G_DONE };
#define G_SECTIONS (5)
#define CHIP_H     (72)
#define CHIP_MAX   (16)
typedef struct { int x, y, w, group, value; const char *label; } chip_t;
static bool s_menu;
static chip_t s_chip[CHIP_MAX];
static int s_chip_n, s_head_x[G_SECTIONS], s_head_y[G_SECTIONS], s_pan_x, s_pan_y, s_pan_w, s_pan_h, s_title_y;

/* landscape first, as the board starts; each followed by its flip */
static const uint8_t ROT_ORDER[4] = { 1, 0, 3, 2 };
static const char *const ROT_NAMES[4] = { N_("Landscape"), N_("Portrait"), N_("Landscape, flipped"), N_("Portrait, flipped") };
static const char *const THEME_NAMES[2] = { N_("Light"), N_("Dark") };
static const char *const AUTO_NAMES[2] = { N_("Off"), N_("On") };
static const uint8_t LANG_ORDER[I18N_LANG_COUNT] = { I18N_EN, I18N_JA, I18N_ZH_CN };
static const char *const HEADS[G_SECTIONS] = { N_("Orientation"), N_("Theme"), N_("Language"), N_("Auto-sort"), N_("Discard sets") };

static bool chip_on(const chip_t *c)
{
    switch (c->group) {
    case G_ROT:   return c->value == s_prefs.rotation;
    case G_THEME: return c->value == s_prefs.theme;
    case G_LANG:  return c->value == (int)i18n_lang();
    case G_AUTO:  return c->value == (s_prefs.auto_sort ? 1 : 0);
    case G_DSET:  return c->value == (s_prefs.discard_set ? 1 : 0);
    default:      return false;
    }
}

static chip_t *add_chip(int group, int value, const char *label, int min_w)
{
    chip_t *c = &s_chip[s_chip_n++];
    c->group = group; c->value = value; c->label = label;
    c->w = gfx_text_w(label, TXT) + 40;
    if (c->w < min_w) c->w = min_w;
    return c;
}

/* The settings fill the screen: New deal and Share at the top, then the
 * settings under their heading, then Done, then the licences of what
 * this firmware was built from (libversions.h, generated when the build
 * is configured). Taller than the screen in landscape, so it scrolls
 * under a finger like the Sets list, and a tap is a press and release
 * that did not move. Every position here is in the panel; s_mscroll
 * moves it up the screen. */
#define M_PAD     (28)
#define M_GAP     (12)
#define LIC_LINE  (GFX_GLYPH_H(SMALL) + 8)
static int s_mscroll, s_menu_h, s_lic_y, s_lic_cx[3];
static bool s_lic_two;        /* too narrow for three columns: each entry on two lines */
static struct { bool down, drag, ignore; int x0, y0, scroll0; } s_mtouch;   /* ignore: the press that opened it */
static const char *const LIC_HEADS[3] = { N_("Module"), N_("Checkout"), N_("Licence") };

static int menu_max_scroll(void) { const int m = s_menu_h - gfx_h(); return m > 0 ? m : 0; }

static void layout_menu(void)
{
    const int pw = gfx_w() - 2 * M_X;
    const int cols = L.land ? 2 : 1, iw = pw - 2 * M_PAD, cw = (iw - (cols - 1) * 32) / cols;
    s_chip_n = 0;
    int y = 20;

    /* the game's two actions, first */
    const bool armed = esp_timer_get_time() < s_armed_until;
    chip_t *nd = add_chip(G_NEW, 0, armed ? _("Tap again to deal") : _("New deal"), 180);
    nd->x = M_PAD; nd->y = y;
    chip_t *sh = add_chip(G_SHARE, 0, _("Share"), 150);
    sh->x = nd->x + nd->w + M_GAP; sh->y = y;
    y += CHIP_H + 28;

    /* the settings, under their heading */
    s_title_y = y;
    const int top = s_title_y + GFX_GLYPH_H(TXT) + 28;
    int col_y[2] = { top, top };
    for (int g = 0; g < G_SECTIONS; g++) {
        /* landscape: orientation and theme on the left, the rest on the right */
        const int col = cols == 2 ? (g < G_LANG ? 0 : 1) : 0, x0 = M_PAD + col * (cw + 32);
        int gy = col_y[col];
        s_head_x[g] = x0;
        s_head_y[g] = gy;
        gy += GFX_GLYPH_H(TXT) + 10;
        const int n = g == G_ROT ? 4 : g == G_LANG ? I18N_LANG_COUNT : 2;
        int cx = x0;
        for (int i = 0; i < n; i++) {
            /* the languages by their own names, which no reader needs translated */
            chip_t *c = g == G_ROT ? add_chip(g, ROT_ORDER[i], _(ROT_NAMES[i]), 128)
                      : g == G_THEME ? add_chip(g, i, _(THEME_NAMES[i]), 128)
                      : g == G_AUTO || g == G_DSET ? add_chip(g, i, _(AUTO_NAMES[i]), 128)
                      : add_chip(g, LANG_ORDER[i], i18n_lang_name((i18n_lang_t)LANG_ORDER[i]), 128);
            if (cx > x0 && cx + c->w > x0 + cw) { cx = x0; gy += CHIP_H + M_GAP; }
            c->x = cx; c->y = gy;
            cx += c->w + M_GAP;
        }
        col_y[col] = gy + CHIP_H + 26;
    }
    y = (col_y[0] > col_y[1] ? col_y[0] : col_y[1]) + 4;

    /* Done, closing the settings */
    chip_t *d = add_chip(G_DONE, 0, _("Done"), 200);
    d->x = pw - M_PAD - d->w; d->y = y;
    y += CHIP_H + 40;

    /* the licences: module, checkout and licence in columns, each as wide
     * as its widest entry */
    s_lic_y = y;
    int w[3] = { 0, 0, 0 };
    for (int c = 0; c < 3; c++) w[c] = gfx_text_w(_(LIC_HEADS[c]), SMALL);
    for (int i = 0; i < LIBVERSIONS_COUNT; i++)
        for (int c = 0; c < 3; c++) { const int tw = gfx_text_w(k_libversions[i][c], SMALL); if (tw > w[c]) w[c] = tw; }
    s_lic_cx[0] = M_PAD;
    s_lic_cx[1] = s_lic_cx[0] + w[0] + 28;
    s_lic_cx[2] = s_lic_cx[1] + w[1] + 28;
    /* in portrait the three do not fit across: the module on one line,
     * its checkout and licence indented under it */
    s_lic_two = s_lic_cx[2] + w[2] > pw - M_PAD;
    if (s_lic_two) { s_lic_cx[1] = M_PAD + 32; s_lic_cx[2] = s_lic_cx[1] + w[1] + 28; }
    y += GFX_GLYPH_H(TXT) + 28 + LIC_LINE * (1 + LIBVERSIONS_COUNT * (s_lic_two ? 2 : 1)) + 24;

    s_pan_x = (gfx_w() - pw) / 2;
    s_pan_y = 0;
    s_pan_w = pw;
    s_pan_h = s_menu_h = y;
    if (s_mscroll > menu_max_scroll()) s_mscroll = menu_max_scroll();
    if (s_mscroll < 0) s_mscroll = 0;
    const int dy = -s_mscroll;
    s_title_y += dy;
    s_lic_y += dy;
    for (int g = 0; g < G_SECTIONS; g++) { s_head_x[g] += s_pan_x; s_head_y[g] += dy; }
    for (int i = 0; i < s_chip_n; i++) { s_chip[i].x += s_pan_x; s_chip[i].y += dy; }
    for (int c = 0; c < 3; c++) s_lic_cx[c] += s_pan_x;
}

static void draw_menu(void)
{
    layout_menu();
    const int x0 = s_pan_x + M_PAD, rw = s_pan_w - 2 * M_PAD;
    gfx_fill_rect(0, 0, gfx_w(), gfx_h(), C_BG);
    gfx_draw_text(x0, s_title_y, _("Settings"), TXT, rw, C_INK);
    gfx_fill_rect(x0, s_title_y + GFX_GLYPH_H(TXT) + 12, rw, 2, C_LINE);
    for (int g = 0; g < G_SECTIONS; g++)
        gfx_draw_text(s_head_x[g], s_head_y[g], _(HEADS[g]), TXT, rw, C_MUTED);
    const bool armed = esp_timer_get_time() < s_armed_until;
    for (int i = 0; i < s_chip_n; i++) {
        const chip_t *c = &s_chip[i];
        const bool on = chip_on(c) || c->group == G_DONE;
        const uint16_t border = c->group == G_NEW && armed ? C_WARN : on ? C_ACCENT : C_LINE;
        const uint16_t fg = c->group == G_NEW && armed ? C_WARN : on ? C_ON_ACC : C_INK;
        box(c->x, c->y, c->w, CHIP_H, border, on ? C_ACCENT : C_BG);
        gfx_draw_text(c->x + (c->w - gfx_text_w(c->label, TXT)) / 2, c->y + (CHIP_H - GFX_GLYPH_H(TXT)) / 2,
                      c->label, TXT, c->w, fg);
    }

    /* the licences */
    gfx_draw_text(x0, s_lic_y, _("Licences"), TXT, rw, C_INK);
    gfx_fill_rect(x0, s_lic_y + GFX_GLYPH_H(TXT) + 12, rw, 2, C_LINE);
    int ly = s_lic_y + GFX_GLYPH_H(TXT) + 28;
    const int right = s_pan_x + s_pan_w - M_PAD;
    if (s_lic_two) {
        /* the headings on the lines their entries use */
        gfx_draw_text(s_lic_cx[0], ly, _(LIC_HEADS[0]), SMALL, right - s_lic_cx[0], C_MUTED);
        ly += LIC_LINE;
        for (int c = 1; c < 3; c++) gfx_draw_text(s_lic_cx[c], ly, _(LIC_HEADS[c]), SMALL, (c < 2 ? s_lic_cx[2] - 12 : right) - s_lic_cx[c], C_MUTED);
    } else {
        for (int c = 0; c < 3; c++) gfx_draw_text(s_lic_cx[c], ly, _(LIC_HEADS[c]), SMALL, (c < 2 ? s_lic_cx[c + 1] - 12 : right) - s_lic_cx[c], C_MUTED);
    }
    for (int i = 0; i < LIBVERSIONS_COUNT; i++) {
        ly += LIC_LINE;
        if (s_lic_two) {
            gfx_draw_text(s_lic_cx[0], ly, k_libversions[i][0], SMALL, right - s_lic_cx[0], C_INK);
            ly += LIC_LINE;
            gfx_draw_text(s_lic_cx[1], ly, k_libversions[i][1], SMALL, s_lic_cx[2] - 12 - s_lic_cx[1], C_MUTED);
            gfx_draw_text(s_lic_cx[2], ly, k_libversions[i][2], SMALL, right - s_lic_cx[2], C_MUTED);
            continue;
        }
        if (ly + LIC_LINE < 0 || ly > gfx_h()) continue;
        for (int c = 0; c < 3; c++)
            gfx_draw_text(s_lic_cx[c], ly, k_libversions[i][c], SMALL, (c < 2 ? s_lic_cx[c + 1] - 12 : right) - s_lic_cx[c], c ? C_MUTED : C_INK);
    }

    /* where in the panel the screen is */
    const int max = menu_max_scroll();
    if (max > 0) {
        const int track = gfx_h() - 16, bar = track * gfx_h() / s_menu_h;
        gfx_fill_rect(gfx_w() - 8, 8 + (track - bar) * s_mscroll / max, 4, bar, C_LINE);
    }
}

static void draw_guide(void);
static void draw_share(void);
static bool s_share;

static void draw_banner(void);

static void redraw(void)
{
    if (s_guide) { draw_guide(); gfx_blit(0, gfx_h()); return; }
    if (s_share) { draw_share(); gfx_blit(0, gfx_h()); return; }
    gfx_fill_rect(0, 0, gfx_w(), gfx_h(), C_BG);
    gfx_fill_rect(M_X / 2, L.table_y, gfx_w() - M_X, L.btn_y - L.table_y - 12, C_SURFACE);
    draw_top();
    draw_rail();
    draw_melds();
    draw_hand();
    draw_status();
    draw_buttons();
    if (esp_timer_get_time() < s_ban.until) draw_banner();
    if (s_menu) draw_menu();
    gfx_blit(0, gfx_h());
}

/* ------------------------------------------------------------------ */
/* The limit-hand celebration                                          */

/* A set's name in another language: what the celebration shows under
 * the hand's own name. The lookup goes through the language for a moment;
 * the strings are the table's own, so they outlive the switch back. */
static const char *set_name_in(um_set_t t, i18n_lang_t lg)
{
    const i18n_lang_t was = i18n_lang();
    i18n_set_lang(lg);
    const char *s = set_name(t);
    i18n_set_lang(was);
    return s;
}

/* Any marked string in another language, the same way. */
static const char *tr_in(const char *key, i18n_lang_t lg)
{
    const i18n_lang_t was = i18n_lang();
    i18n_set_lang(lg);
    const char *s = _(key);
    i18n_set_lang(was);
    return s;
}

/* The hand in its sets, as the Sets list groups them (um_limit_groups):
 * each set drops in together, a beat after the one before, and is named
 * under it; then the hand's name, its name in another language (the
 * Japanese for English, the English for Japanese and Chinese), and the
 * points counting up, as the web version's celebration does. */
#define CEL_GAP   (22)                  /* between sets */
#define CEL_DROP  (48)                  /* how far a set falls */
#define CEL_CAP_H (GFX_GLYPH_H(SMALL) + 6)
#define CEL_ROW   (tilefaces_M.h + TILEDRAW_EDGE_M + 6 + CEL_CAP_H + 14)

typedef struct {
    int ng, row_y0, rows;
    uint8_t k[UM_MAX_SEL], glen[UM_MAX_GROUPS];
    int16_t tx[UM_MAX_SEL], ty[UM_MAX_SEL];     /* each tile's place */
    int16_t gx0[UM_MAX_GROUPS], gx1[UM_MAX_GROUPS], gy[UM_MAX_GROUPS]; /* each set's span, for its name */
    const char *cap[UM_MAX_GROUPS];
} cel_t;
static cel_t s_cel;

static void cel_draw_group(const cel_t *c, int g, int dy)
{
    int at = 0;
    for (int i = 0; i < g; i++) at += c->glen[i];
    for (int j = 0; j < c->glen[g]; j++)
        tiledraw_tile(&tilefaces_M, c->k[at + j], c->tx[at + j], c->ty[at + j] + dy, TILEDRAW_EDGE_M);
}

static void cel_draw_cap(const cel_t *c, int g)
{
    if (!c->cap[g]) return;
    const int w = gfx_text_w(c->cap[g], SMALL);
    gfx_draw_text((c->gx0[g] + c->gx1[g] - w) / 2, c->gy[g] + tilefaces_M.h + TILEDRAW_EDGE_M + 6, c->cap[g], SMALL, w + 2, C_MUTED);
}

/* kick: the line above, for an achievement (a limit hand's own when
 * NULL); name_key: the achievement's name, a marked string, when it is
 * not the set's. Any set can be celebrated: one that splits into sets
 * (double and triple chows, the classic hands, two pair, full house) is
 * shown in them, as the web version does, anything else as one row. */
static void celebrate(um_set_t t, const uint8_t *kinds, int n, int v, const char *kick_in, const char *name_key)
{
    cel_t *c = &s_cel;
    memset(c, 0, sizeof *c);
    const bool split = um_set_is_limit(t) || um_set_is_classic(t) || t == UM_TWO_PAIR || t == UM_FULL_HOUSE ||
                       (t >= UM_MIXED_DOUBLE_CHOW && t <= UM_PURE_TRIPLE_CHOW);
    if (split) c->ng = um_limit_groups(t, kinds, n, c->k, c->glen);
    else if (n >= 1 && n <= UM_MAX_SEL) {
        memcpy(c->k, kinds, (size_t)n);
        for (int i = 1; i < n; i++) for (int j = i; j > 0 && c->k[j] < c->k[j - 1]; j--) { const uint8_t x = c->k[j]; c->k[j] = c->k[j - 1]; c->k[j - 1] = x; }
        c->glen[0] = (uint8_t)n;
        c->ng = 1;
    }
    if (c->ng < 1) return;

    /* lay the sets out in rows: a set that fits a row is not split, a run
     * too long for one (Thirteen Orphans in portrait) wraps; then each
     * row is centred */
    const int pitch = tilefaces_M.w + 2, avail = gfx_w() - 2 * M_X;
    int x = 0, row = 0, at = 0;
    int16_t row_w[UM_MAX_SEL] = { 0 };
    for (int g = 0; g < c->ng; g++) {
        const int gw = c->glen[g] * pitch - 2;
        if (x > 0 && x + gw > avail && gw <= avail) { x = 0; row++; }
        for (int j = 0; j < c->glen[g]; j++, at++) {
            if (x > 0 && x + tilefaces_M.w > avail) { x = 0; row++; }
            c->tx[at] = (int16_t)x;
            c->ty[at] = (int16_t)row;
            if (x + tilefaces_M.w > row_w[row]) row_w[row] = (int16_t)(x + tilefaces_M.w);
            x += pitch;
        }
        x += CEL_GAP - 2;
        /* one-run hands (Thirteen Orphans, Nine Gates) are named by the title alone */
        c->cap[g] = c->ng > 1 ? set_name(um_classify(&c->k[at - c->glen[g]], c->glen[g])) : NULL;
    }
    c->rows = row + 1;

    static char nmb[96], altb[96];
    const char *kick = kick_in ? kick_in : _("LIMIT HAND");
    const i18n_lang_t other = i18n_lang() == I18N_EN ? I18N_JA : I18N_EN;
    const char *nm = name_key ? _(name_key) : heading(set_name(t), nmb, sizeof nmb, i18n_lang() == I18N_EN);
    const char *alt = name_key ? tr_in(name_key, other) : heading(set_name_in(t, other), altb, sizeof altb, other == I18N_EN);
    const int ns = gfx_text_w(nm, 5) <= avail ? 5 : 4;       /* portrait is 720 wide */
    const int total = GFX_GLYPH_H(TXT) + 24 + c->rows * CEL_ROW + 16 + GFX_GLYPH_H(ns) + 12 +
                      GFX_GLYPH_H(TXT) + 24 + GFX_GLYPH_H(4) + 40 + GFX_GLYPH_H(TXT);
    int y = (gfx_h() - total) / 2;
    if (y < 16) y = 16;
    const int ky = y;
    c->row_y0 = ky + GFX_GLYPH_H(TXT) + 24 + CEL_DROP / 2;
    for (int i = 0; i < n; i++) {
        const int r = c->ty[i];
        c->tx[i] = (int16_t)((gfx_w() - row_w[r]) / 2 + c->tx[i]);
        c->ty[i] = (int16_t)(c->row_y0 + r * CEL_ROW);
    }
    at = 0;
    for (int g = 0; g < c->ng; g++) {
        const int last = at + c->glen[g] - 1;
        c->gx0[g] = c->tx[at];
        c->gx1[g] = (int16_t)(c->tx[last] + tilefaces_M.w);
        c->gy[g] = c->ty[last];
        at += c->glen[g];
    }

    gfx_fill_rect(0, 0, gfx_w(), gfx_h(), C_SURFACE);
    gfx_draw_text((gfx_w() - gfx_text_w(kick, TXT)) / 2, ky, kick, TXT, gfx_w(), C_KICK);
    gfx_blit(0, gfx_h());

    /* each set falls into place, eased out; the band it falls through is
     * cleared and everything already down is drawn again over it */
    for (int g = 0; g < c->ng; g++) {
        /* the band runs from the set's first row (a run can wrap) to its last */
        int first = 0;
        for (int d = 0; d < g; d++) first += c->glen[d];
        int top = c->ty[first] - CEL_DROP, bot = c->gy[g] + tilefaces_M.h + TILEDRAW_EDGE_M;
        if (top < 0) top = 0;
        for (int f = 0; f <= 7; f++) {
            const float p = (float)f / 7.0f, e = (1.0f - p) * (1.0f - p) * (1.0f - p);
            gfx_fill_rect(0, top, gfx_w(), bot - top, C_SURFACE);
            gfx_draw_text((gfx_w() - gfx_text_w(kick, TXT)) / 2, ky, kick, TXT, gfx_w(), C_KICK);
            for (int d = 0; d < g; d++) { cel_draw_group(c, d, 0); cel_draw_cap(c, d); }
            cel_draw_group(c, g, -(int)(CEL_DROP * e + 0.5f));
            gfx_blit(top, bot);
            vTaskDelay(pdMS_TO_TICKS(16));
        }
        cel_draw_cap(c, g);
        gfx_blit(c->gy[g] + tilefaces_M.h, c->gy[g] + tilefaces_M.h + TILEDRAW_EDGE_M + 6 + CEL_CAP_H);
        vTaskDelay(pdMS_TO_TICKS(140));
    }

    /* the name, then the other language's beneath it */
    const int ny = c->row_y0 + c->rows * CEL_ROW + 8;
    gfx_draw_text((gfx_w() - gfx_text_w(nm, ns)) / 2, ny, nm, ns, gfx_w(), C_INK);
    gfx_blit(ny, ny + GFX_GLYPH_H(ns));
    vTaskDelay(pdMS_TO_TICKS(150));
    const int ay = ny + GFX_GLYPH_H(ns) + 12;
    gfx_draw_text((gfx_w() - gfx_text_w(alt, TXT)) / 2, ay, alt, TXT, gfx_w(), C_MUTED);
    gfx_blit(ay, ay + GFX_GLYPH_H(TXT));

    /* the points count up, eased out, as on the web */
    char ps[16];
    const int py = ay + GFX_GLYPH_H(TXT) + 24;
    for (int f = 0; f <= 30; f++) {
        const float p = (float)f / 30.0f, e = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
        snprintf(ps, sizeof ps, "+%d", (int)(v * e + 0.5f));
        gfx_fill_rect(0, py, gfx_w(), GFX_GLYPH_H(4), C_SURFACE);
        gfx_draw_text((gfx_w() - gfx_text_w(ps, 4)) / 2, py, ps, 4, gfx_w(), C_GREEN);
        gfx_blit(py, py + GFX_GLYPH_H(4));
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    const char *tap = _("Tap to continue");
    const int ty = py + GFX_GLYPH_H(4) + 40;
    gfx_draw_text((gfx_w() - gfx_text_w(tap, TXT)) / 2, ty, tap, TXT, gfx_w(), C_MUTED);
    gfx_blit(ty, ty + GFX_GLYPH_H(TXT));
    /* until a tap, or 4 s */
    touch_swallow();
    int tx, tyy;
    for (int ms = 0; ms < 4200; ms += 20) {
        if (touch_get(&tx, &tyy)) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    touch_swallow();
}

/* ------------------------------------------------------------------ */
/* Sets: the valid hands                                              */

/* The web version's "Sets and scores", from guide.c: every set with an
 * example, in one list that scrolls under a finger. The points are what
 * the rules here give the example, as the web version's are. Tapping a
 * set shows its note along the bottom; tapping a limit hand also plays
 * its celebration, as a tap on one does on the web. In landscape the
 * sets sit two to a row; in portrait, one.
 *
 * Here a tap is a press and release that did not move, not a press, so
 * a finger can land on a set to start a drag without choosing it. */
#define G_NAME_H   (GFX_GLYPH_H(TXT))
#define G_HEAD_H   (GFX_GLYPH_H(SMALL) + 20)
#define G_PITCH    (tilefaces_M.w + 2)
#define G_LINE     (tilefaces_M.h + TILEDRAW_EDGE_M + 8)
#define G_GROUP    (12)                 /* between a set's groups */
#define G_COL_GAP  (32)
#define G_ITEMS    (64)
#define GD_SECTIONS (8)
#define G_SLOP     (16)                 /* px a press may wander and still be a tap */

typedef struct { int x, y, h; } gpos_t;     /* in the list, before scrolling */
static gpos_t s_gpos[G_ITEMS];
static int s_ghead_y[GD_SECTIONS], s_glist_h, s_gscroll;
static button_t s_gdone;
static struct { bool down, drag, ignore; int x0, y0, scroll0; } s_gtouch;   /* ignore: the press that opened the list */

static int guide_cols(void) { return L.land ? 2 : 1; }
static int guide_col_w(void) { return (gfx_w() - 2 * M_X - (guide_cols() - 1) * G_COL_GAP) / guide_cols(); }
static int guide_btn_y(void) { return L.land ? L.btn_y : gfx_h() - 8 - BTN_H; }
static int guide_note_rows(void) { return L.land ? 2 : 3; }
static int guide_note_y(void) { return guide_btn_y() - 12 - guide_note_rows() * LINE; }
static int guide_top(void) { return TOP_Y + GFX_GLYPH_H(TXT) + 16; }       /* the list's window */
static int guide_bottom(void) { return guide_note_y() - 16; }
static int guide_max_scroll(void) { const int m = s_glist_h - (guide_bottom() - guide_top()); return m > 0 ? m : 0; }

/* Where each of a set's tiles goes, from its tiles' top left: a group
 * that fits a row is not split across two. Returns the rows used. */
static int guide_tiles(const guide_item_t *g, int w, int *tx, int *ty)
{
    int x = 0, row = 0, i = 0;
    for (int gi = 0; gi < g->n_groups; gi++) {
        const int gw = g->glen[gi] * G_PITCH - 2;
        if (x > 0 && x + gw > w && gw <= w) { x = 0; row++; }
        for (int j = 0; j < g->glen[gi]; j++, i++) {
            if (x > 0 && x + tilefaces_M.w > w) { x = 0; row++; }
            if (tx) { tx[i] = x; ty[i] = row * G_LINE; }
            x += G_PITCH;
        }
        x += G_GROUP - 2;
    }
    return row + 1;
}

/* What the achievements say of a set's card: a First (done), and Full.
 * The runs card stands for every run length, as on the web: done when
 * any length has a First, full when any is Full. */
static bool is_run(um_set_t t) { return t >= UM_RUN4 && t <= UM_RUN8; }
static void item_marks(um_set_t t, bool *done, bool *full)
{
    *done = *full = false;
    for (int r = UM_RUN4; r <= UM_RUN8; r++) if (is_run(t)) { *done |= s_ach.first[r].game != 0; *full |= s_ach.full[r].game != 0; }
    if (!is_run(t) && t > UM_NONE && t < UM_SET_COUNT) { *done = s_ach.first[t].game != 0; *full = s_ach.full[t].game != 0; }
}

/* The marks, drawn: Ark12 has neither a check nor a star. */
static const char *const MARK_TICK[] = { "......#", ".....##", "#...##.", "##.##..", ".###...", "..#...." };
static const char *const MARK_STAR[] = { "...#...", "..###..", "#######", ".#####.", "..###..", ".##.##.", ".#...#." };
#define MARK_PX  (4)
#define MARK_W   (7 * MARK_PX + 12)     /* with the gap before the name */
static void draw_mark_in(const char *const *rows, int nrows, int x, int y, int h, uint16_t c)
{
    y += (h - nrows * MARK_PX) / 2;
    for (int r = 0; r < nrows; r++)
        for (int col = 0; rows[r][col]; col++)
            if (rows[r][col] == '#') gfx_fill_rect(x + col * MARK_PX, y + r * MARK_PX, MARK_PX, MARK_PX, c);
}
static void draw_mark(const char *const *rows, int nrows, int x, int y, uint16_t c) { draw_mark_in(rows, nrows, x, y, G_NAME_H, c); }

/* The banner, over the rail's band: the mark, the set's name, what it
 * earned, and its tiles at the rail's size on the right. One line, so
 * the table below never moves. */
static void draw_banner(void)
{
    const int x0 = M_X / 2, w = gfx_w() - M_X, y0 = L.rail_y - 6, h = L.table_y - 6 - y0;
    box(x0, y0, w, h, s_ban.full ? C_KICK : C_GREEN, C_RAISED);
    int x = x0 + 14;
    if (s_ban.full) draw_mark_in(MARK_STAR, 7, x, y0, h, C_KICK);
    else draw_mark_in(MARK_TICK, 6, x, y0, h, C_GREEN);
    x += MARK_W;
    const int tw = s_ban.n * (tilefaces_S.w + 1), tx = x0 + w - 14 - tw;
    static char nmb[96];
    const char *nm = s_ban.name ? _(s_ban.name) : heading(set_name(s_ban.t), nmb, sizeof nmb, i18n_lang() == I18N_EN);
    const int nw = gfx_text_w(nm, TXT);
    gfx_draw_text(x, y0 + (h - GFX_GLYPH_H(TXT)) / 2, nm, TXT, tx - 16 - x, C_INK);
    x += nw + 20;
    if (x < tx - 40) gfx_draw_text(x, y0 + (h - GFX_GLYPH_H(SMALL)) / 2, s_ban.kick, SMALL, tx - 16 - x, s_ban.full ? C_KICK : C_GREEN);
    for (int i = 0; i < s_ban.n; i++) tiledraw_tile(&tilefaces_S, s_ban.k[i], tx + i * (tilefaces_S.w + 1), y0 + (h - tilefaces_S.h) / 2, TILEDRAW_EDGE_S);
}

#define G_WHEN_H   (GFX_GLYPH_H(SMALL) + 10)  /* the line under a set's tiles: the games, or the run lengths */
#define C_RIVER    (0xBC21)                 /* the river's gold, #B8860B, in either theme */
static int guide_item_h(const guide_item_t *g, int w)
{
    bool done, full;
    item_marks(um_classify(g->k, g->n), &done, &full);
    return G_NAME_H + 8 + guide_tiles(g, w, NULL, NULL) * G_LINE + (done ? G_WHEN_H : 0) + 12;
}

/* The achievements, above the sets: a summary, then the web version's
 * Achievements section as cards: The Yellow River Floods Again and The
 * Capital is Gone across the width, each with its river, then the wins,
 * High score and any secrets found, in columns. s_akind says what each
 * card is this time, s_acards how many there are. */
enum { AK_FLOOD, AK_CAPITAL, AK_WIN, AK_HIGH, AK_SECRET };
#define A_CARDS    (2 + UM_WIN_N + 1 + UM_SECRET_N)
#define A_WIDE     (2)      /* the rivers' cards, first */
#define A_BAR_H    (12)
#define A_SUM_LINE (GFX_GLYPH_H(SMALL) + 4)
static gpos_t s_apos[A_CARDS];
static struct { uint8_t kind, i; } s_akind[A_CARDS];
static int s_ahead_y, s_asum_y;
static char s_asum[320];
static int s_acards;
static int ach_card_h(void) { return G_NAME_H + 8 + A_BAR_H + 8 + G_WHEN_H + 12; }
static void ach_cards(void)
{
    int n = 0;
    s_akind[n].kind = AK_FLOOD; s_akind[n++].i = 0;
    s_akind[n].kind = AK_CAPITAL; s_akind[n++].i = 0;
    for (int w = 0; w < UM_WIN_N; w++) { s_akind[n].kind = AK_WIN; s_akind[n++].i = (uint8_t)w; }
    s_akind[n].kind = AK_HIGH; s_akind[n++].i = 0;
    for (int k = 0; k < UM_SECRET_N; k++) if (s_ach.secret[k].game) { s_akind[n].kind = AK_SECRET; s_akind[n++].i = (uint8_t)k; }
    s_acards = n;
}
static const char *ach_card_name(int c)
{
    const int i = s_akind[c].i;
    switch (s_akind[c].kind) {
    case AK_FLOOD:   return FLOOD_NAME;
    case AK_CAPITAL: return CAPITAL_NAME;
    case AK_WIN:     return WIN_NAMES[i];
    case AK_HIGH:    return HIGH_NAME;
    default:         return SECRET_NAMES[i];
    }
}
static const char *ach_card_note(int c)
{
    const int i = s_akind[c].i;
    switch (s_akind[c].kind) {
    case AK_FLOOD:   return FLOOD_NOTE;
    case AK_CAPITAL: return CAPITAL_NOTE;
    case AK_WIN:     return WIN_NOTES[i];
    case AK_HIGH:    return HIGH_NOTE;
    default:         return SECRET_NOTES[i];
    }
}
static const um_rec_t *ach_card_rec(int c)
{
    const int i = s_akind[c].i;
    switch (s_akind[c].kind) {
    case AK_FLOOD:   return &s_ach.flood;
    case AK_CAPITAL: return &s_ach.capital;
    case AK_WIN:     return &s_ach.win[i];
    case AK_HIGH:    return &s_ach.high;
    default:         return &s_ach.secret[i];
    }
}

/* Each section: its heading across the width, then its sets, left to
 * right and down, so the list reads in order as it scrolls. */
static void guide_layout(void)
{
    const int cols = guide_cols(), cw = guide_col_w();
    int y = 0;
    s_ahead_y = y;
    y += G_HEAD_H;
    snprintf(s_asum, sizeof s_asum, _("%d of %d sets cleared at least once, %d of them full (cleared in 3 of 5 games in a row). Games played: %d."),
             um_ach_firsts(&s_ach), UM_ACH_SETS, um_ach_fulls(&s_ach), (int)s_ach.games);
    s_asum_y = y;
    y += gfx_para_rows(s_asum, SMALL, gfx_w() - 2 * M_X) * A_SUM_LINE + 12;
    /* the rivers across the whole width, the rest in columns under them */
    ach_cards();
    for (int i = 0; i < A_WIDE; i++) {
        s_apos[i] = (gpos_t){ M_X, y, ach_card_h() };
        y += ach_card_h();
    }
    for (int i = A_WIDE; i < s_acards; i += cols) {
        for (int c = 0; c < cols && i + c < s_acards; c++) s_apos[i + c] = (gpos_t){ M_X + c * (cw + G_COL_GAP), y, ach_card_h() };
        y += ach_card_h();
    }
    y += 8;
    for (int s = 0; s < guide_section_count && s < GD_SECTIONS; s++) {
        const guide_section_t *sec = &guide_sections[s];
        s_ghead_y[s] = y;
        y += G_HEAD_H;
        for (int i = sec->first; i < sec->first + sec->count && i < G_ITEMS; i += cols) {
            int row_h = 0;
            for (int c = 0; c < cols && i + c < sec->first + sec->count && i + c < G_ITEMS; c++) {
                const int h = guide_item_h(&guide_items[i + c], cw);
                s_gpos[i + c] = (gpos_t){ M_X + c * (cw + G_COL_GAP), y, h };
                if (h > row_h) row_h = h;
            }
            y += row_h;
        }
        y += 8;
    }
    s_glist_h = y;
    if (s_gscroll > guide_max_scroll()) s_gscroll = guide_max_scroll();
    if (s_gscroll < 0) s_gscroll = 0;

    s_gdone.label = _("Done");
    s_gdone.w = gfx_text_w(s_gdone.label, TXT) + 48;
    if (s_gdone.w < 200) s_gdone.w = 200;
    s_gdone.x = gfx_w() - M_X - s_gdone.w;
    s_gdone.y = guide_btn_y();
}

static void draw_guide(void)
{
    guide_layout();
    const int cw = guide_col_w(), top = guide_top(), bottom = guide_bottom(), dy = top - s_gscroll;
    gfx_fill_rect(0, 0, gfx_w(), gfx_h(), C_BG);

    /* the list, drawn whole where it falls; what spills past its window
     * is painted over by the title and the footer below */
    for (int s = 0; s < guide_section_count && s < GD_SECTIONS; s++) {
        const int hy = s_ghead_y[s] + dy;
        if (hy + G_HEAD_H > top - G_HEAD_H && hy < bottom)
            gfx_draw_text(M_X, hy + 8, _(guide_sections[s].head), SMALL, gfx_w() - 2 * M_X, C_MUTED);
    }
    gfx_draw_text(M_X, s_ahead_y + dy + 8, _("Achievements"), SMALL, gfx_w() - 2 * M_X, C_MUTED);
    gfx_draw_para(M_X, s_asum_y + dy, s_asum, SMALL, gfx_w() - 2 * M_X, A_SUM_LINE, 4, C_MUTED);
    for (int i = 0; i < s_acards; i++) {
        const gpos_t *p = &s_apos[i];
        const int y = p->y + dy;
        if (y + p->h < top || y > bottom) continue;
        const int cw = i >= A_WIDE ? guide_col_w() : gfx_w() - 2 * M_X;     /* the rivers' cards are the whole width */
        if (i == s_gach_sel) box(p->x - 10, y - 6, cw + 20, p->h, C_ACCENT, C_RAISED);
        const um_rec_t *r = ach_card_rec(i);
        int nx = p->x, nw = cw;
        if (r->game) { draw_mark(MARK_TICK, 6, p->x, y, C_GREEN); nx += MARK_W; nw -= MARK_W; }
        char when[96] = "";
        gfx_draw_text(nx, y, _(ach_card_name(i)), TXT, nw, C_INK);
        /* a river, rising with each First (or Full), and how far it has
         * come; the rest have none */
        const int by = y + G_NAME_H + 8, kind = s_akind[i].kind;
        if (i < A_WIDE) {
            const int got = kind == AK_FLOOD ? um_ach_firsts(&s_ach) : um_ach_fulls(&s_ach);
            char v[24];
            snprintf(v, sizeof v, "%d/%d", got, UM_ACH_SETS);
            const int vw = gfx_text_w(v, SMALL), bw = cw - vw - 16;
            gfx_fill_rect(p->x, by, bw, A_BAR_H, C_LINE);
            gfx_fill_rect(p->x, by, bw * got / UM_ACH_SETS, A_BAR_H, C_RIVER);
            gfx_draw_text(p->x + cw - vw, by + (A_BAR_H - GFX_GLYPH_H(SMALL)) / 2, v, SMALL, vw + 2, C_GREEN);
        }
        if (r->game) snprintf(when, sizeof when,
                              kind == AK_FLOOD ? _("flooded in game %d") : kind == AK_CAPITAL ? _("fallen in game %d")
                              : kind == AK_SECRET ? _("found in game %d") : kind == AK_HIGH ? _("scored in game %d") : _("won in game %d"), (int)r->game);
        else if (i >= A_WIDE) snprintf(when, sizeof when, "%s", _("not yet"));
        gfx_draw_text(p->x, by + A_BAR_H + 8, when, SMALL, cw, r->game ? C_GREEN : C_MUTED);
    }
    int tx[GUIDE_MAX_TILES], ty[GUIDE_MAX_TILES];
    for (int i = 0; i < guide_item_count && i < G_ITEMS; i++) {
        const gpos_t *p = &s_gpos[i];
        const int y = p->y + dy;
        if (y + p->h < top || y > bottom) continue;
        const guide_item_t *g = &guide_items[i];
        if (i == s_guide_sel) box(p->x - 10, y - 6, cw + 20, p->h, C_ACCENT, C_RAISED);
        char v[16];
        const um_set_t t = um_classify(g->k, g->n);
        snprintf(v, sizeof v, "+%d", t ? um_set_value(t, g->k, g->n) : 0);
        const int vw = gfx_text_w(v, TXT);
        bool done, full;
        item_marks(t, &done, &full);
        int nx = p->x;
        if (full) draw_mark(MARK_STAR, 7, p->x, y, C_KICK);
        else if (done) draw_mark(MARK_TICK, 6, p->x, y, C_GREEN);
        if (done) nx += MARK_W;
        gfx_draw_text(nx, y, _(g->name), TXT, cw - vw - 16 - (nx - p->x), C_INK);
        gfx_draw_text(p->x + cw - vw, y, v, TXT, vw + 2, C_GREEN);
        const int rows = guide_tiles(g, cw, tx, ty);
        for (int j = 0; j < g->n; j++)
            tiledraw_tile(&tilefaces_M, g->k[j], p->x + tx[j], y + G_NAME_H + 8 + ty[j], TILEDRAW_EDGE_M);
        if (!done) continue;
        /* under the tiles: the games it was first cleared and went full
         * in; for the runs card, each run length, lit once cleared and
         * filled once full */
        const int wy = y + G_NAME_H + 8 + rows * G_LINE;
        if (is_run(t)) {
            const int bw = 44, bh = GFX_GLYPH_H(SMALL) + 4;
            for (int r = UM_RUN4; r <= UM_RUN8; r++) {
                const int bx = p->x + (r - UM_RUN4) * (bw + 8);
                const bool on = s_ach.first[r].game != 0, fu = s_ach.full[r].game != 0;
                box(bx, wy, bw, bh, on ? C_GREEN : C_LINE, fu ? C_GREEN : C_RAISED);
                char d[4];
                snprintf(d, sizeof d, "%d", r - UM_RUN4 + 4);
                gfx_draw_text(bx + (bw - gfx_text_w(d, SMALL)) / 2, wy + 2, d, SMALL, bw, fu ? C_SURFACE : on ? C_GREEN : C_MUTED);
            }
        } else {
            char when[128];
            size_t at = 0;
            put(when, sizeof when, &at, _("first in game %d"), (int)s_ach.first[t].game);
            if (full) { put(when, sizeof when, &at, " · "); put(when, sizeof when, &at, _("full in game %d"), (int)s_ach.full[t].game); }
            gfx_draw_text(p->x, wy, when, SMALL, cw, C_GREEN);
        }
    }

    /* the title over the top of the window, the footer over the bottom */
    gfx_fill_rect(0, 0, gfx_w(), top, C_BG);
    text(M_X, TOP_Y, _("Sets and scores"), C_INK);
    gfx_fill_rect(0, bottom, gfx_w(), gfx_h() - bottom, C_BG);
    gfx_fill_rect(M_X, top - 2, gfx_w() - 2 * M_X, 2, C_LINE);       /* the window's edges */
    gfx_fill_rect(M_X, bottom, gfx_w() - 2 * M_X, 2, C_LINE);

    /* where in the list the window is */
    const int max = guide_max_scroll();
    if (max > 0) {
        const int track = bottom - top, bar = track * track / s_glist_h;
        const int by = top + (track - bar) * s_gscroll / max;
        gfx_fill_rect(gfx_w() - 8, by, 4, bar, C_LINE);
    }

    /* the chosen set's note, or how to get one */
    const int ny = guide_note_y();
    if (s_gach_sel >= 0) gfx_draw_para(M_X, ny, _(ach_card_note(s_gach_sel)), TXT, gfx_w() - 2 * M_X, LINE, guide_note_rows(), C_INK);
    else if (s_guide_sel >= 0) gfx_draw_para(M_X, ny, _(guide_items[s_guide_sel].note), TXT, gfx_w() - 2 * M_X, LINE, guide_note_rows(), C_INK);
    else gfx_draw_para(M_X, ny, _("Drag to scroll. Tap a set to read about it."), TXT, gfx_w() - 2 * M_X, LINE, guide_note_rows(), C_MUTED);

    box(s_gdone.x, s_gdone.y, s_gdone.w, BTN_H, C_ACCENT, C_ACCENT);
    gfx_draw_text(s_gdone.x + (s_gdone.w - gfx_text_w(s_gdone.label, TXT)) / 2, s_gdone.y + (BTN_H - GFX_GLYPH_H(TXT)) / 2,
                  s_gdone.label, TXT, s_gdone.w, C_ON_ACC);
}

/* A tap: a press and release that stayed within G_SLOP. */
static void guide_tap(int x, int y)
{
    guide_layout();
    if (x >= s_gdone.x && x < s_gdone.x + s_gdone.w && y >= s_gdone.y && y < s_gdone.y + BTN_H) {
        s_guide = false;
        s_guide_sel = -1;
        s_gach_sel = -1;
        return;
    }
    if (y < guide_top() || y >= guide_bottom()) return;
    const int cw = guide_col_w(), ly = y - guide_top() + s_gscroll;
    for (int i = 0; i < s_acards; i++) {
        const gpos_t *p = &s_apos[i];
        const int aw = i >= A_WIDE ? cw : gfx_w() - 2 * M_X;
        if (x < p->x - 10 || x >= p->x + aw + 10 || ly < p->y - 6 || ly >= p->y - 6 + p->h) continue;
        s_gach_sel = i;
        s_guide_sel = -1;
        return;
    }
    for (int i = 0; i < guide_item_count && i < G_ITEMS; i++) {
        const gpos_t *p = &s_gpos[i];
        if (x < p->x - 10 || x >= p->x + cw + 10 || ly < p->y - 6 || ly >= p->y - 6 + p->h) continue;
        s_guide_sel = i;
        s_gach_sel = -1;
        const guide_item_t *g = &guide_items[i];
        const um_set_t t = um_classify(g->k, g->n);
        if (um_set_is_limit(t)) celebrate(t, g->k, g->n, um_set_value(t, g->k, g->n), NULL, NULL);
        return;
    }
}

/* The finger, every pass of the loop while the list is open. Returns
 * whether the screen needs drawing. */
static bool guide_touch(bool down, int x, int y)
{
    if (s_gtouch.ignore) {
        if (!down) s_gtouch.ignore = false;
        return false;
    }
    if (down && !s_gtouch.down) {
        s_gtouch.down = true;
        s_gtouch.drag = false;
        s_gtouch.x0 = x;
        s_gtouch.y0 = y;
        s_gtouch.scroll0 = s_gscroll;
        return false;
    }
    if (down) {
        const int d = y - s_gtouch.y0;
        if (!s_gtouch.drag && (d > G_SLOP || d < -G_SLOP || x - s_gtouch.x0 > G_SLOP || s_gtouch.x0 - x > G_SLOP))
            s_gtouch.drag = s_gtouch.y0 >= guide_top() && s_gtouch.y0 < guide_bottom();
        if (!s_gtouch.drag) return false;
        int to = s_gtouch.scroll0 - d;
        if (to > guide_max_scroll()) to = guide_max_scroll();
        if (to < 0) to = 0;
        if (to == s_gscroll) return false;
        s_gscroll = to;
        return true;
    }
    if (!s_gtouch.down) return false;
    s_gtouch.down = false;
    if (s_gtouch.drag) return false;
    guide_tap(s_gtouch.x0, s_gtouch.y0);
    return true;
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
        /* A Place for Everything first, as the web version's winAwards */
        const bool place = um_all_cleared(&s_g) && s_g.log_n && um_ach_secret(&s_ach, &s_g, UM_SECRET_PLACE);
        const uint8_t wins = um_ach_win(&s_ach, &s_g);
        const bool high = um_ach_high(&s_ach, &s_g);
        if (high) {
            ESP_LOGI(TAG, "game %u: High score, %d points", (unsigned)s_g.game_no, (int)s_g.points);
            save_ach(&s_ach);
        }
        if (wins || place) {
            ESP_LOGI(TAG, "game %u:%s%s%s%s", (unsigned)s_g.game_no, place ? " a place for everything" : "",
                     wins & (1u << UM_WIN_NO_POKER) ? ", won with no poker hands" : "", wins & (1u << UM_WIN_NO_PAIRS) ? ", won with no pairs" : "",
                     wins & (1u << UM_WIN_ALL_POKER) ? ", won with every poker hand" : "");
            save_ach(&s_ach);
            /* each celebrates with the last set cleared and the game's points */
            const um_log_t *e = &s_g.log[(s_g.log_n - 1) % UM_LOG_N];
            static char kick[64], skick[64];
            snprintf(kick, sizeof kick, _("WIN · GAME %d"), (int)s_g.game_no);
            snprintf(skick, sizeof skick, _("SECRET · GAME %d"), (int)s_g.game_no);
            if (place && e->t < UM_SET_COUNT) celebrate((um_set_t)e->t, e->k, e->n, (int)s_g.points, skick, SECRET_NAMES[UM_SECRET_PLACE]);
            for (int w = 0; w < UM_WIN_N && e->t < UM_SET_COUNT; w++)
                if (wins & (1u << w)) celebrate((um_set_t)e->t, e->k, e->n, (int)s_g.points, kick, WIN_NAMES[w]);
        }
        /* High score after the wins, as highAward after winAwards: with
         * the last set cleared, which a stuck game may have further back */
        if (high) {
            static char hkick[64];
            snprintf(hkick, sizeof hkick, _("500 POINTS · GAME %d"), (int)s_g.game_no);
            const int back = s_g.log_n < UM_LOG_N ? (int)s_g.log_n : UM_LOG_N;
            for (int j = 1; j <= back; j++) {
                const um_log_t *e = &s_g.log[(s_g.log_n - j) % UM_LOG_N];
                if (e->t > UM_NONE && e->t < UM_SET_COUNT) { celebrate((um_set_t)e->t, e->k, e->n, (int)s_g.points, hkick, HIGH_NAME); break; }
            }
        }
    }
    s_sel.hand = 0;
    s_sel.melds = 0;
    s_hint_until = 0;
    save_game(&s_g);
}

/* A secret earned away from a clear, shown in the banner with no tiles. */
static void secret_banner(int s)
{
    if (!um_ach_secret(&s_ach, &s_g, s)) return;
    ESP_LOGI(TAG, "game %u: secret %s", (unsigned)s_g.game_no, SECRET_NAMES[s]);
    save_ach(&s_ach);
    s_ban.name = SECRET_NAMES[s];
    s_ban.full = true;
    s_ban.n = 0;
    snprintf(s_ban.kick, sizeof s_ban.kick, _("SECRET · GAME %d"), (int)s_g.game_no);
    s_ban.until = esp_timer_get_time() + BANNER_US;
}
/* A sort threw the selection away: Wait Wait Wait, the first time. */
static void sorted_away(void) { secret_banner(UM_SECRET_WAIT); }

static void new_deal(void)
{
    um_new(&s_g, esp_random());
    um_ach_new_game(&s_ach, &s_g);
    s_ban.until = 0;
    save_ach(&s_ach);
    s_undo_n = 0;
    after_move(false);
}

static void press(int b)
{
    if (!s_btn[b].enabled) {
        /* Meld stays drawn disabled, but a press on a melded pair and
         * its third tile is caught: Intentional Game Design */
        if (b == B_MELD && um_pair_grow(&s_g, s_sel)) secret_banner(UM_SECRET_DESIGN);
        return;
    }
    const bool was_done = s_g.done;
    switch (b) {
    case B_CLEAR: {
        uint8_t k[UM_MAX_SEL];
        const int n = um_sel_kinds(&s_g, s_sel, k);
        um_set_t t;
        int v;
        undo_push();
        if (!um_clear(&s_g, s_sel, &t, &v)) { undo_pop(); return; }
        bool flood = false, capital = false;
        const um_award_t aw = um_ach_clear(&s_ach, &s_g, t, &flood, &capital);
        if (aw != UM_AWARD_NONE) {
            ESP_LOGI(TAG, "game %u: %s %s%s", (unsigned)s_g.game_no, aw == UM_AWARD_FIRST ? "first" : "full", um_set_key(t),
                     flood ? "; The Yellow River Floods Again" : "");
            save_ach(&s_ach);
        }
        /* the set's own celebration, then the flood's: each waits for a tap */
        static char kick[96];
        const int gn = (int)s_g.game_no;
        if (aw == UM_AWARD_FIRST && um_set_is_limit(t)) snprintf(kick, sizeof kick, _("FIRST LIMIT HAND · GAME %d"), gn);
        else if (aw == UM_AWARD_FIRST) snprintf(kick, sizeof kick, _("FIRST TIME · GAME %d"), gn);
        else if (aw == UM_AWARD_FULL) snprintf(kick, sizeof kick, _("FULL · 3 OF 5 GAMES · GAME %d"), gn);
        if (um_set_is_limit(t)) celebrate(t, k, n, v, aw != UM_AWARD_NONE ? kick : NULL, NULL);
        else if (aw != UM_AWARD_NONE) {
            /* a new one replaces any still showing */
            s_ban.t = t;
            s_ban.name = NULL;
            s_ban.full = aw == UM_AWARD_FULL;
            s_ban.n = (uint8_t)n;
            memcpy(s_ban.k, k, (size_t)n);
            for (int i = 1; i < n; i++) for (int j = i; j > 0 && s_ban.k[j] < s_ban.k[j - 1]; j--) { const uint8_t x = s_ban.k[j]; s_ban.k[j] = s_ban.k[j - 1]; s_ban.k[j - 1] = x; }
            snprintf(s_ban.kick, sizeof s_ban.kick, "%s", kick);
            s_ban.until = esp_timer_get_time() + BANNER_US;
        }
        if (flood) {
            snprintf(kick, sizeof kick, _("EVERY SET · GAME %d"), gn);
            celebrate(t, k, n, UM_ACH_SETS, kick, FLOOD_NAME);
        }
        if (capital) {
            ESP_LOGI(TAG, "game %u: The Capital is Gone", (unsigned)s_g.game_no);
            snprintf(kick, sizeof kick, _("EVERY SET FULL · GAME %d"), gn);
            celebrate(t, k, n, UM_ACH_SETS, kick, CAPITAL_NAME);
        }
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
        if (!(discard_kind() == 2 ? um_discard_set(&s_g, s_sel, NULL) : um_discard_six(&s_g, s_sel))) return;
        s_undo_n = 0;       /* and so is discarding */
        break;
    case B_HINT: {
        um_sel_t h;
        s_sel.hand = 0; s_sel.melds = 0;
        s_hint_mask = um_hint(&s_g, &h) ? h.hand : 0;
        s_hint_until = esp_timer_get_time() + HINT_US;
        return;
    }
    case B_SORT: {
        const bool chosen = s_sel.hand || s_sel.melds;
        undo_push();
        um_sort(&s_g);
        if (chosen) sorted_away();
        break;
    }
    case B_UNDO:
        if (!undo_pop()) return;
        s_sel.hand = 0; s_sel.melds = 0;
        save_game(&s_g);
        return;
    default:
        return;
    }
    after_move(was_done);
}

/* ------------------------------------------------------------------ */
/* Share: the game's replay link as a QR code                          */

/* The full link, so a phone's camera opens the replay on the web: the
 * page reads the code, checks it and plays the game back move by move.
 * Error correction is the highest (H, about 30% of the code can be lost),
 * which leaves room for the 1 Bamboo over the middle; the site is written
 * underneath. The QR is black on white whatever the theme, which is what
 * scanners expect. */
/* sudrien.github.io/umj forwards to the game, #replay= and all: 17
 * characters fewer than the game's own address, so a smaller code */
#define REPLAY_URL  "https://sudrien.github.io/umj/#replay="
#define REPLAY_SITE "sudrien.github.io/umj"
#ifndef UI_REPLAY_WORK
#define UI_REPLAY_WORK UM_REPLAY_WORK   /* test/screen may lower it, under the sanitizers */
#endif

static bool s_share_ok;
/* The icon's height, as a share of the code's: decided by decoding the
 * screen test's shots with two decoders, shrunk, blurred and skewed as a
 * phone sees them. test/screen sets it from SCREENTEST_QR_ICON to try others. */
int ui_qr_icon_pct = 30;      /* s_share, the view's open flag, is declared above redraw() */
/* about 12 KB between them: in PSRAM, made the first time Share is used */
static struct {
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
    char code[UM_CODE_MAX], url[sizeof REPLAY_URL + UM_CODE_MAX];
} *s_sh;

static void share_open(void)
{
    /* the proof of work takes a moment: say so first */
    gfx_fill_rect(0, 0, gfx_w(), gfx_h(), C_BG);
    const char *wk = _("Working…");
    gfx_draw_text((gfx_w() - gfx_text_w(wk, TXT)) / 2, gfx_h() / 2 - GFX_GLYPH_H(TXT) / 2, wk, TXT, gfx_w(), C_MUTED);
    gfx_blit(0, gfx_h());
    const int64_t t0 = esp_timer_get_time();
    if (!s_sh) s_sh = heap_caps_malloc(sizeof *s_sh, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const int n = s_sh ? um_replay_code(&s_g, esp_random(), UI_REPLAY_WORK, s_sh->code, sizeof s_sh->code) : -1;
    s_share_ok = n > 0;
    if (s_share_ok) {
        snprintf(s_sh->url, sizeof s_sh->url, "%s%s", REPLAY_URL, s_sh->code);
        s_share_ok = qrcodegen_encodeText(s_sh->url, s_sh->tmp, s_sh->qr, qrcodegen_Ecc_HIGH,
                                          qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, false);
        ESP_LOGI(TAG, "replay code: %d chars, QR %d modules, %lld ms", n, s_share_ok ? qrcodegen_getSize(s_sh->qr) : 0,
                 (long long)((esp_timer_get_time() - t0) / 1000));
        ESP_LOGI(TAG, "%s", s_sh->url);
    }
    (void)t0;   /* only the log reads it */
    s_share = true;
    touch_swallow();
}

static void draw_share(void)
{
    gfx_fill_rect(0, 0, gfx_w(), gfx_h(), C_BG);
    const char *tap = _("Tap to close");
    if (!s_share_ok) {
        gfx_draw_para(M_X * 2, gfx_h() / 2 - LINE * 2,
                      _("This game cannot be shared: it was dealt before replay codes, or ran too long. Start a new deal to get one."),
                      TXT, gfx_w() - 4 * M_X, LINE, 4, C_INK);
        gfx_draw_text((gfx_w() - gfx_text_w(tap, SMALL)) / 2, gfx_h() - 60, tap, SMALL, gfx_w(), C_MUTED);
        return;
    }
    const char *title = _("Scan to watch this game");
    const int top = TOP_Y + GFX_GLYPH_H(TXT) + 24, foot = GFX_GLYPH_H(TXT) + 16 + GFX_GLYPH_H(SMALL) + 24;
    gfx_draw_text((gfx_w() - gfx_text_w(title, TXT)) / 2, TOP_Y + 8, title, TXT, gfx_w(), C_INK);

    /* the code, four modules of quiet zone round it, as large as fits */
    const int size = qrcodegen_getSize(s_sh->qr), span = size + 8;
    const int aw = gfx_w() - 2 * M_X, ah = gfx_h() - top - foot;
    const int m = (aw < ah ? aw : ah) / span;
    const int px = m * span, x0 = (gfx_w() - px) / 2, y0 = top + (ah - px) / 2;
    const uint16_t white = RGB(0xff, 0xff, 0xff), black = RGB(0, 0, 0);
    gfx_fill_rect(x0, y0, px, px, white);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            if (qrcodegen_getModule(s_sh->qr, x, y)) gfx_fill_rect(x0 + (x + 4) * m, y0 + (y + 4) * m, m, m, black);

    /* the 1 Bamboo over the middle, on a white margin a module wide, its
     * height ui_qr_icon_pct of the code's (the quiet zone not counted) */
    const int cx = x0 + px / 2, cy = y0 + px / 2;
    const int ih = size * m * ui_qr_icon_pct / 100, edge = ih / 20 > 2 ? ih / 20 : 2;
    const int fh = ih - edge, fw = fh * tilefaces_L.w / tilefaces_L.h;
    gfx_fill_rect(cx - fw / 2 - m, cy - ih / 2 - m, fw + 2 * m, ih + 2 * m, white);
    tiledraw_tile_scaled(&tilefaces_L, 18, cx - fw / 2, cy - ih / 2, fw, fh, edge);

    /* the site, and how to leave */
    const int sy = y0 + px + 16;
    const int ss = gfx_text_w(REPLAY_SITE, TXT) <= gfx_w() - 2 * M_X ? TXT : SMALL;    /* portrait is 720 wide */
    gfx_draw_text((gfx_w() - gfx_text_w(REPLAY_SITE, ss)) / 2, sy, REPLAY_SITE, ss, gfx_w(), C_INK);
    gfx_draw_text((gfx_w() - gfx_text_w(tap, SMALL)) / 2, sy + GFX_GLYPH_H(ss) + 16, tap, SMALL, gfx_w(), C_MUTED);
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
        case G_AUTO:
            s_prefs.auto_sort = (uint8_t)c->value;
            if (s_prefs.auto_sort) {
                /* as the web version: the choice goes, and the first time, that is Wait Wait Wait */
                const bool chosen = s_sel.hand || s_sel.melds;
                s_sel.hand = 0; s_sel.melds = 0;
                undo_push(); um_sort(&s_g); save_game(&s_g);
                if (chosen) sorted_away();
            }
            break;
        case G_DSET:
            s_prefs.discard_set = (uint8_t)c->value;
            break;
        case G_SHARE:
            s_menu = false;
            share_open();
            return;
        case G_NEW:
            if (esp_timer_get_time() < s_armed_until) { s_armed_until = 0; s_menu = false; new_deal(); }
            else s_armed_until = esp_timer_get_time() + ARM_US;
            return;
        default:
            s_menu = false;
            return;
        }
        save_prefs(&s_prefs);
        return;
    }
}

/* The finger, every pass of the loop while the settings are open.
 * Returns whether the screen needs drawing. */
static bool menu_touch(bool down, int x, int y)
{
    if (s_mtouch.ignore) {
        if (!down) s_mtouch.ignore = false;
        return false;
    }
    if (down && !s_mtouch.down) {
        s_mtouch.down = true;
        s_mtouch.drag = false;
        s_mtouch.x0 = x;
        s_mtouch.y0 = y;
        s_mtouch.scroll0 = s_mscroll;
        return false;
    }
    if (down) {
        const int d = y - s_mtouch.y0;
        if (!s_mtouch.drag && (d > G_SLOP || d < -G_SLOP || x - s_mtouch.x0 > G_SLOP || s_mtouch.x0 - x > G_SLOP))
            s_mtouch.drag = menu_max_scroll() > 0;
        if (!s_mtouch.drag) return false;
        int to = s_mtouch.scroll0 - d;
        if (to > menu_max_scroll()) to = menu_max_scroll();
        if (to < 0) to = 0;
        if (to == s_mscroll) return false;
        s_mscroll = to;
        return true;
    }
    if (!s_mtouch.down) return false;
    s_mtouch.down = false;
    if (s_mtouch.drag) return false;
    menu_tap(s_mtouch.x0, s_mtouch.y0);
    return true;
}

static void tap(int x, int y)
{
    if (s_share) { s_share = false; return; }   /* any tap closes the QR */
    {
        int sx0, sy0, sw, sh;
        sets_rect(&sx0, &sy0, &sw, &sh);
        if (x >= sx0 && x < sx0 + sw && y >= sy0 && y < sy0 + sh) { s_guide = true; s_guide_sel = -1; s_gach_sel = -1; s_hint_until = 0; s_gtouch.ignore = true; return; }
    }
    {
        int gx, gy, gw, gh;
        gear_rect(&gx, &gy, &gw, &gh);
        if (x >= gx && x < gx + gw && y >= gy && y < gy + gh) { s_menu = true; s_mscroll = 0; s_mtouch.ignore = true; s_mtouch.down = false; s_hint_until = 0; s_armed_until = 0; return; }
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
    save_load_ach(&s_ach);
    i18n_set_lang((i18n_lang_t)s_prefs.lang);
    apply_theme(s_prefs.theme);
    apply_rotation(s_prefs.rotation);
    if (save_load_game(&s_g)) ESP_LOGI(TAG, "resumed a saved game");
    else new_deal();

    bool was_down = false, dirty = true;
    int64_t hint_seen = 0, armed_seen = 0, ban_seen = 0;
    for (;;) {
        int x = 0, y = 0;
        const bool down = touch_get(&x, &y);
        if (s_rdrag.active) {
            if (rail_touch(down, x)) dirty = true;
        } else if (s_guide) {
            if (guide_touch(down, x, y)) dirty = true;
        } else if (s_menu) {
            if (menu_touch(down, x, y)) dirty = true;
        } else if (down && !was_down && !s_menu && rail_hit(x, y)) {
            s_rdrag.active = true;          /* a press on the rail starts a drag, not a tap */
            s_rdrag.x0 = x;
            s_rdrag.scroll0 = s_rail_scroll;
        } else if (down && !was_down) {
            tap(x, y);
            dirty = true;
        }
        was_down = down;
        /* the hint glow, the armed New deal (in the panel) and the banner time out on their own */
        const int64_t now = esp_timer_get_time();
        const bool hint_on = now < s_hint_until, armed_on = now < s_armed_until, ban_on = now < s_ban.until;
        if (hint_on != (hint_seen != 0) || armed_on != (armed_seen != 0) || ban_on != (ban_seen != 0)) dirty = true;
        hint_seen = hint_on; armed_seen = armed_on; ban_seen = ban_on;
        if (dirty) { redraw(); dirty = false; }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}
