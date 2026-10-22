/*
 * qrscan.h -- a replay link's QR code, read from a grey camera frame.
 *
 * quirc (components/quirc) finds and decodes the codes in the frame;
 * this keeps the first whose text is a replay link, or a bare replay
 * code, and hands back the code itself -- the base64url after
 * "#replay=" -- for umreplay to read. No ESP-IDF headers: test/
 * builds it on the host (scancheck).
 *
 * Not thread-safe: one task scans.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QRSCAN_CODE_MAX (1024)  /* a whole game's code is about 150; the longest a QR code can hold is under 3000 */

/* The replay code in a QR code's text: what follows "#replay=" in a
 * link, or the whole text when it is nothing but base64url. Copies it,
 * NUL-terminated, into code (cap bytes). False when the text is neither,
 * or the code would not fit. */
bool qrscan_code_in(const char *text, size_t len, char *code, size_t cap);

/* Look for a replay link's QR code in a w x h grey frame (one byte a
 * pixel, rows packed). On finding one, its code goes into code as
 * qrscan_code_in puts it and the result is true. *seen, if not NULL,
 * is how many QR codes were found at all, read or not -- enough to say
 * "hold steady" rather than "point at a code". The first call at a size
 * allocates quirc's buffers (about w*h bytes, in PSRAM through malloc);
 * they are kept for the next frame. */
bool qrscan_frame(const uint8_t *grey, int w, int h, char *code, size_t cap, int *seen);

/* Free quirc's buffers. */
void qrscan_free(void);

#ifdef __cplusplus
}
#endif
