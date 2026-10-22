# SPDX-License-Identifier: MIT
idf_component_register(
    SRCS "main.c" "ui.c" "tiledraw.c" "save.c" "tilefaces.c" "i18n.c" "i18n_tab.c" "guide.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer nvs_flash esp_hw_support esp_lcd esp_app_format
             feckless_graphics_handler   # gfx, lcd, ark12
             feckless_drivers            # tab5io, touch
             umcore                      # the rules and replay codes, from ../core
             qrcodegen                   # the replay link's QR code
             feckless_camera             # probed at boot; nothing reads it yet
)
