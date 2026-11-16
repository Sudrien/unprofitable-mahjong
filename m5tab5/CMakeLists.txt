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

# u-unprofit-0003: feckless_box_libc is the ELF's (elf/ links it in), never
# this firmware's. It is here all the same: main's manifest names it, so the
# component manager makes it one of main's requirements and its archive is
# on the .bin's link line, ahead of the firmware's own libc. Something in the
# .bin (the camera's libraries, at a guess) asked for stdio and strerror,
# took the box's, and the link stopped on stdin, stdout, stderr, strerror, getc, fgetc, putc and
# fputc defined twice. So in this build, and only this one, the archive is
# built with nothing in it. elf/ is a project of its own and builds it whole.
idf_component_get_property(_box_libc feckless_box_libc COMPONENT_LIB)
file(WRITE "${CMAKE_BINARY_DIR}/box_libc_not_here.c"
     "/* u-unprofit-0003: feckless_box_libc, left out of the firmware */\ntypedef int box_libc_not_here_t;\n")
set_property(TARGET ${_box_libc} PROPERTY SOURCES "${CMAKE_BINARY_DIR}/box_libc_not_here.c")

# libversions.h, the settings' list of licences, is elf/'s now: the
# game, which includes it, is built there.
