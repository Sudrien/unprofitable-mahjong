# SPDX-License-Identifier: MIT
#
# Unprofitable Mahjong for the M5Stack Tab5. Build from this folder:
#
#   idf.py set-target esp32p4
#   idf.py build flash monitor
#
# The rules live in ./core, built as the umcore
# component (components/umcore), and are tested on the host by test/.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Build only main and what it REQUIRES, as the player does (its 6134):
# main names its requirements explicitly, so nothing is lost but build time.
idf_build_set_property(MINIMAL_BUILD ON)

project(unprofitable_mahjong_m5tab5)
