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
feckless_abi_elf(unprofitable_mahjong)

# libversions.h, the settings' list of licences, which ui.c includes.
idf_component_get_property(main_lib main COMPONENT_LIB)
include(${_m5}/cmake/libversions.cmake)
target_include_directories(${main_lib} PRIVATE "${LIBVERSIONS_DIR}")
