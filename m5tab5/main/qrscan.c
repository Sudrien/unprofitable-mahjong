/*
 * qrscan.c -- see qrscan.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "qrscan.h"

#include <string.h>

#include "quirc.h"

static struct quirc *s_q;
static int s_w, s_h;

/* base64url, the replay code's alphabet (umreplay.c's B64) */
static bool b64url(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

bool qrscan_code_in(const char *text, size_t len, char *code, size_t cap)
{
    static const char KEY[] = "#replay=";
    const size_t kl = sizeof KEY - 1;
    size_t from = 0;
    bool found = false;
    for (size_t i = 0; i + kl <= len; i++)
        if (!memcmp(text + i, KEY, kl)) { from = i + kl; found = true; break; }
    size_t n = 0;
    if (found) {
        /* the code runs to the first character outside its alphabet */
        while (from + n < len && b64url(text[from + n])) n++;
    } else {
        /* a bare code: every character in the alphabet */
        for (size_t i = 0; i < len; i++) if (!b64url(text[i])) return false;
        n = len;
    }
    if (!n || n + 1 > cap) return false;
    memcpy(code, text + from, n);
    code[n] = 0;
    return true;
}

bool qrscan_frame(const uint8_t *grey, int w, int h, char *code, size_t cap, int *seen)
{
    if (seen) *seen = 0;
    if (!grey || w <= 0 || h <= 0) return false;
    if (!s_q) {
        s_q = quirc_new();
        if (!s_q) return false;
        s_w = s_h = 0;
    }
    if (w != s_w || h != s_h) {
        if (quirc_resize(s_q, w, h) < 0) { qrscan_free(); return false; }
        s_w = w;
        s_h = h;
    }
    int qw, qh;
    uint8_t *img = quirc_begin(s_q, &qw, &qh);
    memcpy(img, grey, (size_t)w * (size_t)h);
    quirc_end(s_q);

    const int n = quirc_count(s_q);
    if (seen) *seen = n;
    /* static: struct quirc_code is about 4 KB and quirc_data 9 KB, too
     * much for a task's stack */
    static struct quirc_code qc;
    static struct quirc_data qd;
    for (int i = 0; i < n; i++) {
        quirc_extract(s_q, i, &qc);
        quirc_decode_error_t err = quirc_decode(&qc, &qd);
        if (err == QUIRC_ERROR_DATA_ECC) {
            /* seen in a mirror, as a front camera does */
            quirc_flip(&qc);
            err = quirc_decode(&qc, &qd);
        }
        if (err) continue;
        if (qrscan_code_in((const char *)qd.payload, (size_t)qd.payload_len, code, cap)) return true;
    }
    return false;
}

void qrscan_free(void)
{
    if (s_q) quirc_destroy(s_q);
    s_q = NULL;
    s_w = s_h = 0;
}
