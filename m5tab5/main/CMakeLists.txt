# SPDX-License-Identifier: MIT
idf_component_register(
    SRCS "main.c" "ui.c" "tiledraw.c" "save.c" "tilefaces.c" "qricon.c" "i18n.c" "i18n_tab.c" "guide.c" "qrscan.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer nvs_flash esp_hw_support esp_lcd esp_app_format
             feckless_graphics_handler   # gfx, lcd, ark12
             feckless_drivers            # tab5io, touch
             umcore                      # the rules and replay codes, from ../core
             qrcodegen                   # the replay link's QR code
             quirc                       # reading one off the camera (qrscan.c)
             feckless_camera             # Scan a code (ui.c), through qrscan.c
)
