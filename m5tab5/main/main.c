/*
 * main.c -- bring the Tab5 up and hand over to the game.
 *
 * The order is the player's (defeatist-music-player-for-m5tab5's
 * app_main), which is the order the hardware needs: the I2C bus and the
 * expanders first, because they hold the panel's and the touch
 * controller's reset lines; then the panel; then touch, which has
 * nothing to answer on the bus until the panel's reset is released.
 *
 * The game is an ELF built against the feckless ABI (../elf/), with
 * NVS and the camera from its nvs and camera sections: this app_main()
 * is what a launcher such as Avoidant runs, and standalone.c is the
 * .bin's, which only loads it. The board may already be up; its inits
 * return ESP_OK when called again.
 *
 * SPDX-License-Identifier: MIT
 */
#include "camera.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "feckless_abi.h"
#include "feckless_abi_camera.h"
#include "feckless_abi_nvs.h"
#include "gfx.h"
#include "lcd.h"
#include "tab5io.h"
#include "touch.h"

#include "save.h"
#include "ui.h"

static const char *TAG = "main";

/* The picture and the touch coordinates turn together, or every tap
 * lands somewhere other than where it was drawn. A quarter turn
 * clockwise puts the long edge along the bottom. */
#define ROTATION        GFX_ROT_90
#define BRIGHTNESS_PCT  (80)

void app_main(void)
{
    /* Tie the ELF to the launcher's ABI and to the two sections; a
     * launcher without either refuses it before any of this runs. */
    feckless_abi_require();
    feckless_abi_require_nvs();
    feckless_abi_require_camera();

    /* The line to start copying from, as in the player: everything above
     * it is the ROM, the bootloader and IDF's startup. The version is on
     * it so a pasted log can be matched to a tree; ESP_LOGW so it
     * survives a build with the info level turned down; first, so a
     * failure in the very first init is still below it. */
    {
        const esp_app_desc_t *d = esp_app_get_description();
        ESP_LOGW(TAG, "=== Unprofitable Mahjong === %s, IDF %s, built %s %s",
                 d ? d->version : "?", d ? d->idf_ver : "?",
                 d ? d->date : "?", d ? d->time : "?");
        (void)d;    /* unused when logging is compiled out */
    }
    ESP_ERROR_CHECK(tab5io_init());
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(lcd_init(&panel));
    ESP_ERROR_CHECK(lcd_backlight_set(BRIGHTNESS_PCT));
    if (touch_init(tab5io_bus(), LCD_H_RES, LCD_V_RES) != ESP_OK) {
        ESP_LOGW(TAG, "no touch controller found -- the game can be seen but not played");
    }
    ESP_ERROR_CHECK(gfx_init(panel, LCD_H_RES, LCD_V_RES));
    gfx_set_rotation(ROTATION);
    touch_set_rotation(ROTATION);
    ESP_LOGI(TAG, "screen %dx%d", gfx_w(), gfx_h());

    /* After lcd_init(), which holds the PHY supply the CSI side shares.
     * Only a probe for now, so a board's log says whether the camera
     * answers; seeds by QR code will start it. No camera is no loss. */
    if (camera_init(tab5io_bus()) != ESP_OK) {
        ESP_LOGW(TAG, "no camera -- seeds cannot be read from QR codes");
    }

    save_init();
    ui_run();
}
