/*
 * main.c -- bring the Tab5 up and hand over to the game.
 *
 * The order is the player's (defeatist-music-player-for-m5tab5's
 * app_main), which is the order the hardware needs: the I2C bus and the
 * expanders first, because they hold the panel's and the touch
 * controller's reset lines; then the panel; then touch, which has
 * nothing to answer on the bus until the panel's reset is released.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esp_check.h"
#include "esp_log.h"
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

    save_init();
    ui_run();
}
