# The game as an ELF for esp_elf -- main/'s game sources, linked
# position-independent with the rules, qrcodegen and quirc linked in and
# every other call left undefined: the feckless ABI's, with its nvs and
# camera sections (feckless_abi, feckless_abi_nvs, feckless_abi_camera
# from feckless-libs-for-m5tab5). The build checks the ELF against them.
# The shape is Lothesome Audio Analyzer's elf/.
#
# The top-level build runs this one (main/CMakeLists.txt) and embeds
# what it makes. On its own:
#   idf.py -C elf -B build-elf build
# writes build-elf/unprofitable_mahjong.app.elf.
#
# SPDX-License-Identifier: MIT

# u-unprofit-0005: as the top level's CMakeLists.txt, and for its reason:
# this project has ESP-IDF's bootloader externalproject_add() too.
set(CMAKE_POLICY_DEFAULT_CMP0225 OLD)
cmake_minimum_required(VERSION 3.16)

# The configuration is the .bin's, copied whole when the top-level
# build passes it: struct layouts and inline functions in the headers
# depend on it, and the ELF must agree with the host it runs on. On its
# own, the defaults.
#
# Except the two paths in it relative to the project, which would be
# looked for in elf/: the partition table and the camera's tuning file,
# made absolute, here and in the defaults (the later file wins).
get_filename_component(_m5 "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_partitions_line "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"${_m5}/partitions.csv\"")
set(_ipa_line "CONFIG_CAMERA_SC202CS_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE_PATH=\"${_m5}/camera/sc202cs_ipa.json\"")
if(UM_HOST_SDKCONFIG AND SDKCONFIG)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${UM_HOST_SDKCONFIG}")
    file(READ "${UM_HOST_SDKCONFIG}" _cfg)
    string(REGEX REPLACE "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"[^\"\n]*\""
           "${_partitions_line}" _cfg "${_cfg}")
    string(REGEX REPLACE "CONFIG_CAMERA_SC202CS_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE_PATH=\"[^\"\n]*\""
           "${_ipa_line}" _cfg "${_cfg}")
    file(WRITE "${SDKCONFIG}.host" "${_cfg}")
    configure_file("${SDKCONFIG}.host" "${SDKCONFIG}" COPYONLY)
endif()
file(WRITE "${CMAKE_BINARY_DIR}/sdkconfig.elf.defaults" "${_partitions_line}\n${_ipa_line}\n")
set(SDKCONFIG_DEFAULTS "${_m5}/sdkconfig.defaults"
                       "${CMAKE_BINARY_DIR}/sdkconfig.elf.defaults")

# main is ../main, the rules, qrcodegen and quirc are ../components; only
# what main needs is built. ../components/esp_ipa comes too, as it does
# for the .bin: feckless_camera's headers bring esp_video into the build,
# though nothing of it is linked into the ELF.
list(APPEND EXTRA_COMPONENT_DIRS "${_m5}/main" "${_m5}/components")
set(COMPONENTS main)

# The .bin's version, which CI takes from the release tag (version.txt
# beside the .bin's CMakeLists.txt, not here), so the ELF's own
# esp_app_desc_t says the same. On its own, IDF's usual: git describe.
if(UM_PROJECT_VER)
    set(PROJECT_VER "${UM_PROJECT_VER}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Tells main/CMakeLists.txt which of its two builds this is.
idf_build_set_property(UM_ELF 1)
# One manifest, so one lock: the top level's.
idf_build_set_property(DEPENDENCIES_LOCK "${_m5}/dependencies.lock")

project(unprofitable_mahjong)

# Into the ELF, not the ABI: the game's own libraries.
set(ELF_COMPONENTS umcore qrcodegen quirc)
# u-unprofit-0001: for a box (feckless-libs' umode branch): the pure part of
# libc, its maths and heap_caps linked into the ELF, and a check that it
# imports none of it and nothing a box does not answer.
feckless_abi_box_elf(unprofitable_mahjong)

# libversions.h, the settings' list of licences, which ui.c includes:
# feckless_libversions() since 0097, which reads the lock, the vendored
# components with a manifest (esp_ipa), the fonts and this repository's
# commit. What only this game knows comes as EXTRA rows: qrcodegen and
# quirc at the commit each README names, the tile faces' glyphs and the
# replay code's hash. Writes libversions.tsv beside it, for the docs.
set(_um_extra "")
foreach(_vend "qrcodegen|qrcodegen (Project Nayuki)|MIT"
              "quirc|quirc (Daniel Beer)|ISC")
    string(REPLACE "|" ";" _vl "${_vend}")
    list(GET _vl 0 _dir)
    list(GET _vl 1 _name)
    list(GET _vl 2 _lic)
    set(_rd "${_m5}/components/${_dir}/README.md")
    set(_v "vendored")
    if(EXISTS "${_rd}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_rd}")
        file(READ "${_rd}" _t)
        if(_t MATCHES "commit `([0-9a-f]+)`")
            string(SUBSTRING "${CMAKE_MATCH_1}" 0 7 _v)
        endif()
    endif()
    list(APPEND _um_extra "${_name}|${_v}|${_lic}")
endforeach()
# Pictures of Noto Serif CJK glyphs, rendered by tools/gen_tiles.py: not
# the font, so the OFL asks nothing of them, but it is where they come from.
list(APPEND _um_extra "Noto Serif CJK, tile glyphs|rendered|OFL-1.1")
list(APPEND _um_extra "MurmurHash2 (Austin Appleby)|-|public domain")

idf_component_get_property(main_lib main COMPONENT_LIB)
feckless_libversions(${main_lib} NAME "Unprofitable Mahjong" LICENSE MIT
                     EXTRA ${_um_extra})
