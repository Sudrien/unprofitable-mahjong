/* SPDX-License-Identifier: MIT
 *
 * The .bin's app_main(). The game is the ELF that ../elf/ built from
 * main.c and the rest, embedded in this firmware, and this loads it the
 * way a launcher would load the published copy.
 *
 * NVS comes up here, as a launcher brings it up: the game finds it up.
 *
 * u-unprofit-0001: the game is built for a box, so it runs in one here as
 * on Avoidant's umode branch -- RISC-V user mode, in one block of PSRAM
 * the PMP grants it (and the framebuffer beside it, once it asks for it),
 * every call outside it answered by feckless_box_calls -- and not through
 * feckless_abi_run(), which would refuse it by the name
 * feckless_abi_box_v1. What a launcher does for a boxed app is done here:
 * the board up first, on core 0; the app's NVS namespace named; the app
 * run on core 1.
 */
#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "feckless_abi.h"
#include "feckless_box_calls.h"
#include "feckless_umode_idf.h"
#include "feckless_umode_load.h"
#include "gfx.h"
#include "lcd.h"
#include "nvs_handler.h"
#include "tab5io.h"
#include "touch.h"

extern const uint8_t elf_start[] asm("_binary_unprofitable_mahjong_app_elf_start");
extern const uint8_t elf_end[]   asm("_binary_unprofitable_mahjong_app_elf_end");

/* The game's namespace, save.c's NS: what Avoidant's boxns.c makes of
 * unprofitable_mahjong...elf, so the game keeps one namespace whichever
 * runs it. */
#define GAME_NS  "unprofitable"
/* Where save.c kept everything before u-unprofit-0001: every key it has
 * ever written, since the old namespace is erased once they are moved. */
#define OLD_NS   "umj"
static const nvsh_move_t MOVES[] = {
    { OLD_NS, "game",     NVSH_BLOB },
    { OLD_NS, "ach",      NVSH_BLOB },
    { OLD_NS, "best_pts", NVSH_I32 },
    { OLD_NS, "best_pen", NVSH_I32 },
    { OLD_NS, "autosort", NVSH_U8 },
    { OLD_NS, "discset",  NVSH_U8 },
    { OLD_NS, "lang",     NVSH_U8 },
    { OLD_NS, "rot",      NVSH_U8 },
    { OLD_NS, "theme",    NVSH_U8 },
    { OLD_NS, "goals",    NVSH_U32 },
};

void app_main(void)
{
    /* a failure is logged; the game plays and saves nothing */
    if (nvsh_init() == ESP_OK) {
        /* A Tab5 that ran this firmware before the box has its game, its
         * scores and its settings in "umj": moved once, here, because
         * nvsh_migrate() is not answered in a box. Nothing to move costs
         * a failed read a key. */
        size_t moved = 0;
        const esp_err_t m = nvsh_migrate(GAME_NS, MOVES, sizeof MOVES / sizeof MOVES[0], &moved);
        if (m != ESP_OK) ESP_LOGW("standalone", "moving the saves out of \"" OLD_NS "\": %s", esp_err_to_name(m));
        else if (moved) ESP_LOGW("standalone", "%u saved values moved from \"" OLD_NS "\"", (unsigned)moved);
    }
    /* The one namespace the box answers NVS in; any other reads as empty
     * and refuses a write (feckless_box_calls_nvs()). */
    static const char *const ns[] = { GAME_NS };
    if (!feckless_box_calls_nvs(ns, 1)) ESP_LOGE("standalone", "no NVS namespace for the box -- nothing will be saved");

    /* The board up here first, on core 0, as a launcher has it before an
     * app runs: the panel's interrupts -- its DMA restart, every frame --
     * are then core 0's, and the game runs on core 1
     * (feckless_umode_app_run_on()), where its windows cannot starve them
     * (Lothesome's u-lothesome-0004 has the cyan this avoids). The game's
     * own inits find them up and return ESP_OK. */
    esp_lcd_panel_handle_t panel = NULL;
    if (tab5io_init() != ESP_OK || lcd_init(&panel) != ESP_OK ||
        gfx_init(panel, LCD_H_RES, LCD_V_RES) != ESP_OK) {
        ESP_LOGE("standalone", "the board did not come up");
        return;
    }
    touch_init(tab5io_bus(), LCD_H_RES, LCD_V_RES);     /* the game says so itself if there is none */

    /* The box: the largest PSRAM block less 3 MB for this firmware, at
     * most 16 MB; 64 KB of it app_main's stack, as Avoidant gives. The
     * stack guard's interrupt out of every core's routing first, which a
     * window needs. */
    feckless_umode_guard_unroute();
    const size_t big = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    size_t box = big > (3u << 20) ? big - (3u << 20) : 0;
    if (box > (16u << 20)) box = 16u << 20;
    box &= ~(size_t)63;
    uint32_t n;
    const feckless_umode_call_t *calls = feckless_box_calls_v1(&n);
    feckless_umode_app_t *app = NULL;
    const esp_err_t e = feckless_umode_load(elf_start, (size_t)(elf_end - elf_start), calls, n,
                                            (uint32_t)box, 64 * 1024, &app);
    if (e != ESP_OK) {
        ESP_LOGE("standalone", "the game would not load in a box: %s", esp_err_to_name(e));
        return;
    }
    ESP_LOGI("standalone", "the game in a box of %u KB", (unsigned)(box / 1024));
    const feckless_umode_end_t end = feckless_umode_app_run_on(app, 1);
    ESP_LOGE("standalone", "the game ended: reason %d, %s at 0x%08x", (int)end.reason,
             end.reason == FECKLESS_UMODE_END_FAULT ? feckless_umode_cause_name(end.mcause) : "-",
             (unsigned)end.pc);
    feckless_umode_unload(app);
    feckless_box_calls_end();
}
