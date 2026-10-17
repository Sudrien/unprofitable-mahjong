# SPDX-License-Identifier: MIT
idf_component_register(
    SRCS "main.c" "ui.c" "tiledraw.c" "save.c" "tilefaces.c" "i18n.c" "i18n_tab.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer nvs_flash esp_hw_support esp_lcd esp_app_format
             feckless_graphics_handler   # gfx, lcd, ark12
             feckless_drivers            # tab5io, touch
             umcore                      # the rules, from ../core
)
