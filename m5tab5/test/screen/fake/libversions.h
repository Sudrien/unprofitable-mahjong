/* The screen test's libversions.h: what feckless_libversions() writes
 * for a board build, with this checkout's components, written by hand
 * since the screen test has no CMake configure step.
 *
 * SPDX-License-Identifier: MIT */
#pragma once
#ifndef FECKLESS_LIBV_T
#define FECKLESS_LIBV_T
typedef struct { const char *module, *checkout, *licence; } libv_t;
#endif
#define LIBVERSIONS_COUNT (17)
static const libv_t k_libversions[LIBVERSIONS_COUNT] = {
    { "Unprofitable Mahjong", "screen test", "MIT" },
    { "esp_lcd_st7121", "1.0.1", "Apache-2.0" },
    { "esp_lcd_touch", "1.2.1", "Apache-2.0" },
    { "esp_lcd_touch_gt911", "1.2.1", "Apache-2.0" },
    { "esp_lcd_touch_st7123", "1.0.2", "Apache-2.0" },
    { "usb", "1.6.0", "Apache-2.0" },
    { "usb_host_uac", "1.5.0", "Apache-2.0" },
    { "feckless_drivers", "4d33f10", "MIT" },
    { "feckless_graphics_handler", "286ec6c", "MIT" },
    { "ESP-IDF", "6.1.0", "Apache-2.0" },
    { "esp_ipa", "2.4.0 (vendored)", "Espressif MIT" },
    { "Ark Pixel font (TakWolf)", "d8bd834", "OFL-1.1" },
    { "Galmuri font (Minseo Lee)", "-", "OFL-1.1" },
    { "Arabixel Basic font", "-", "CC-BY-4.0" },
    { "qrcodegen (Project Nayuki)", "720f62b", "MIT" },
    { "Noto Serif CJK, tile glyphs", "rendered", "OFL-1.1" },
    { "MurmurHash2 (Austin Appleby)", "-", "public domain" },
};
