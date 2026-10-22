# SPDX-License-Identifier: MIT (this file; the library is ISC, in LICENSE)
# quirc, Daniel Beer's QR-code recognition library -- vendored as it is:
# see README.md here. Reads a replay link's QR code off the camera
# (main/qrscan.c).
#
# float rather than double: the P4's FPU is single precision.
idf_component_register(
    SRCS "decode.c" "identify.c" "quirc.c" "version_db.c"
    INCLUDE_DIRS "."
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE QUIRC_FLOAT_TYPE=float QUIRC_USE_TGMATH)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter -Wno-sign-compare)
