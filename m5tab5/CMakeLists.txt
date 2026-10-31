# SPDX-License-Identifier: MIT
#
# Unprofitable Mahjong for the M5Stack Tab5. Build from this folder:
#
#   idf.py set-target esp32p4
#   idf.py build flash monitor
#
# The rules live in ./core, built as the umcore
# component (components/umcore), and are tested on the host by test/.
#
# The game itself is an ELF built against the feckless ABI, by elf/;
# this build is main/standalone.c, which embeds that ELF and loads it.
# The same ELF runs on a launcher (Avoidant) with no install. On its own:
#   idf.py -C elf -B build-elf build
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Build only main and what it REQUIRES, as the player does (its 6134):
# main names its requirements explicitly, so nothing is lost but build time.
idf_build_set_property(MINIMAL_BUILD ON)

project(unprofitable_mahjong_m5tab5)

# libversions.h, the settings' list of licences, is elf/'s now: the
# game, which includes it, is built there.
