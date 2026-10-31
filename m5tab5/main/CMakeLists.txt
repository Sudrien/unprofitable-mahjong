# SPDX-License-Identifier: MIT
#
# One directory, two builds. ../elf/ sets UM_ELF and builds the game
# into an ELF for esp_elf, checked against the feckless ABI; the top
# level builds standalone.c, runs ../elf/ and embeds what it made.
idf_build_get_property(_um_elf UM_ELF)

if(_um_elf)
    idf_component_register(
        SRCS "main.c" "ui.c" "tiledraw.c" "save.c" "tilefaces.c" "qricon.c" "i18n.c" "i18n_tab.c" "guide.c" "qrscan.c"
        INCLUDE_DIRS "."
        REQUIRES esp_timer esp_hw_support esp_lcd esp_app_format
                 feckless_abi                # the ABI the ELF is checked against
                 feckless_abi_nvs            # its nvs section: save.c
                 feckless_abi_camera         # its camera section: Scan a code
                 feckless_graphics_handler   # gfx, lcd, ark12
                 feckless_drivers            # tab5io, touch
                 feckless_nvs_handler        # the game, the scores, the settings (save.c)
                 feckless_camera             # Scan a code (ui.c), through qrscan.c
                 umcore                      # the rules and replay codes, from ../core; linked into the ELF
                 qrcodegen                   # the replay link's QR code; linked into the ELF
                 quirc                       # reading one off the camera (qrscan.c); linked into the ELF
    )
    return()
endif()

idf_component_register(
    SRCS "standalone.c"
    PRIV_REQUIRES feckless_abi feckless_abi_nvs feckless_abi_camera feckless_nvs_handler
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include(ExternalProject)
    idf_build_get_property(_idf_path IDF_PATH)
    idf_build_get_property(_target IDF_TARGET)
    idf_build_get_property(_build_dir BUILD_DIR)
    idf_build_get_property(_sdkconfig SDKCONFIG)
    idf_build_get_property(_project_dir PROJECT_DIR)
    idf_build_get_property(_project_ver PROJECT_VER)

    set(_elf_dir "${_build_dir}/elf")
    set(_elf "${_elf_dir}/unprofitable_mahjong.app.elf")

    externalproject_add(um_elf
        SOURCE_DIR "${_project_dir}/elf"
        BINARY_DIR "${_elf_dir}"
        CMAKE_ARGS -DIDF_PATH=${_idf_path}
                   -DIDF_TARGET=${_target}
                   -DSDKCONFIG=${_elf_dir}/sdkconfig
                   -DUM_HOST_SDKCONFIG=${_sdkconfig}
                   -DUM_PROJECT_VER=${_project_ver}
        CMAKE_GENERATOR "Unix Makefiles"
        BUILD_COMMAND make elf
        BUILD_ALWAYS 1
        BUILD_BYPRODUCTS "${_elf}"
        INSTALL_COMMAND ""
    )

    # elf/ checked it against the ABI as it linked it; embedding waits
    # for that.
    add_dependencies(${COMPONENT_LIB} um_elf)

    # elf_loader's: 4-byte aligned, as esp_elf_relocate() wants it.
    elf_embed_aligned_binary(${COMPONENT_LIB} "${_elf}" BINARY)
endif()
