# SPDX-License-Identifier: MIT (this file; the library is MIT too, in its headers)
# QR Code generator library (C), Project Nayuki, MIT -- vendored as it
# is: see README.md here. The replay link's QR code (main/ui.c).
idf_component_register(
    SRCS "qrcodegen.c"
    INCLUDE_DIRS "."
)
