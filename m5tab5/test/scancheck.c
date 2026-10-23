/*
 * scancheck.c -- main/qrscan.c (and quirc under it) on the host.
 *
 * Replay links, as the Tab5 and the web version draw them (qrcodegen,
 * error correction High), are drawn into 640x360 grey frames -- the
 * camera's 1280x720 at every second pixel -- as a phone or a screen held
 * up to the camera shows them: modules of 3.5 px and up, turned up to 10
 * degrees, blurred, dimmed, a little noisy, some mirrored -- and read
 * back. Every one must give back its code. Also: what qrscan_code_in
 * takes and refuses.
 *
 * Smaller, more turned or dimmer than that is past what quirc reads
 * after this blur -- 2.6 px modules blurred, or 3 px turned 11 degrees
 * and blurred, fail -- and past what Scan a code needs: a code held up
 * fills far more of the frame.
 *
 *   scancheck [frames]
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qrcodegen.h"
#include "qrscan.h"

#define W 640
#define H 360
#define URL "https://sudrien.github.io/umj/#replay="
#ifndef MAXLEN
#define MAXLEN 160
#endif
#ifndef TURN
#define TURN 0.087
#endif
#ifndef TRIES
#define TRIES 16
#endif
#ifndef MIN_PX
#define MIN_PX 4.0
#endif

static int s_bad;
#define CHECK(c, ...) do { if (!(c)) { printf("scancheck: " __VA_ARGS__); printf("\n"); s_bad++; } } while (0)

static uint32_t s_rng = 12345;
static uint32_t rnd(void) { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
static double frand(void) { return (rnd() & 0xffffff) / (double)0x1000000; }

static uint8_t s_qr[qrcodegen_BUFFER_LEN_MAX], s_tmp[qrcodegen_BUFFER_LEN_MAX];
static uint8_t s_img[W * H], s_blur[W * H];

/* The QR code drawn into the frame: module m pixels, turned by a
 * radians about the frame's middle, mirrored if asked, on a dim page
 * (paper) in a darker room, then blurred and with noise. */
static void draw(double m, double a, int mirror, int paper, int ink, int noise)
{
    const int n = qrcodegen_getSize(s_qr);
    const double ca = cos(a), sa = sin(a), half = (n + 8) * m / 2;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            /* frame -> code coordinates, the quiet zone of 4 included */
            double dx = x - W / 2.0, dy = y - H / 2.0;
            if (mirror) dx = -dx;
            const double u = (ca * dx + sa * dy + half) / m - 4, v = (-sa * dx + ca * dy + half) / m - 4;
            int c = 60;                                     /* the room */
            if (u >= -4 && v >= -4 && u < n + 4 && v < n + 4) {
                c = paper;
                const int mx = (int)floor(u), my = (int)floor(v);
                if (mx >= 0 && my >= 0 && mx < n && my < n && qrcodegen_getModule(s_qr, mx, my)) c = ink;
            }
            s_img[y * W + x] = (uint8_t)c;
        }
    /* a 3x3 blur, then noise */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int s = 0, k = 0;
            for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
                const int xx = x + i, yy = y + j, wt = (i ? 1 : 2) * (j ? 1 : 2);   /* 1-2-1 each way */
                if (xx >= 0 && yy >= 0 && xx < W && yy < H) { s += wt * s_img[yy * W + xx]; k += wt; }
            }
            int c = s / k + (noise ? (int)(rnd() % (2 * noise + 1)) - noise : 0);
            s_blur[y * W + x] = (uint8_t)(c < 0 ? 0 : c > 255 ? 255 : c);
        }
}

static void code_in_cases(void)
{
    char out[64];
    static const struct { const char *text; const char *want; } C[] = {
        { URL "B9cp4HAAAT4ed6Dnk", "B9cp4HAAAT4ed6Dnk" },
        { "https://example.org/x/index.html#replay=ab-_9Z&utm=1", "ab-_9Z" },
        { "B9cp4HAAAT4ed6Dnk", "B9cp4HAAAT4ed6Dnk" },
        { "file:///home/me/web/index.html#replay=Q", "Q" },
        { "https://example.org/", NULL },
        { "not a code", NULL },
        { URL, NULL },
        { "", NULL },
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        const bool ok = qrscan_code_in(C[i].text, strlen(C[i].text), out, sizeof out);
        CHECK(ok == (C[i].want != NULL) && (!ok || !strcmp(out, C[i].want)), "code_in(\"%s\") gave %s", C[i].text, ok ? out : "nothing");
    }
    CHECK(!qrscan_code_in(URL "0123456789", strlen(URL) + 10, out, 10), "a code longer than its buffer was taken");
}

int main(int argc, char **argv)
{
    const int frames = argc > 1 ? atoi(argv[1]) : 300;
    code_in_cases();

    static char link[QRSCAN_CODE_MAX + 64], want[QRSCAN_CODE_MAX], got[QRSCAN_CODE_MAX];
    static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    int read = 0, mirrored = 0;
    double smallest = 1e9;
    for (int f = 0; f < frames; f++) {
        /* a code the length of a fresh deal's (17) up to a long game's */
        const int len = 17 + (int)(rnd() % (unsigned)(MAXLEN - 16));
        for (int i = 0; i < len; i++) want[i] = B64[rnd() % 64];
        want[len] = 0;
        snprintf(link, sizeof link, "%s%s", URL, want);
        if (!qrcodegen_encodeText(link, s_tmp, s_qr, qrcodegen_Ecc_HIGH, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, false)) {
            CHECK(0, "could not encode a %d-character link", (int)strlen(link));
            continue;
        }
        const int n = qrcodegen_getSize(s_qr);
        /* turned up to TURN radians each way; modules of MIN_PX pixels and
         * up, as large as fits turned */
        const double a = (frand() - .5) * 2 * TURN;
        const double mmax = (H - 8) / ((n + 8) * (cos(a) + fabs(sin(a)))), m = MIN_PX + frand() * (mmax - MIN_PX);
        if (mmax < MIN_PX) continue;    /* a code too long to show that large in the frame */
        const int mirror = f % 7 == 3;
        const int paper = 170 + (int)(rnd() % 80), ink = 20 + (int)(rnd() % 40), noise = (int)(rnd() % 7);
        /* as the camera does: a few frames of a hand that is never quite
         * still -- the turn and the size a touch different each time --
         * and the code read if any of them reads */
        int seen = 0;
        bool ok = false;
        for (int t = 0; t < TRIES && !ok; t++) {
            draw(m * (1 + (frand() - .5) * .04), a + (frand() - .5) * .02, mirror, paper, ink, noise);
            ok = qrscan_frame(s_blur, W, H, got, sizeof got, &seen);
        }
        CHECK(ok && !strcmp(got, want), "frame %d: %d modules of %.2f px%s, %d found: %s", f, n, m, mirror ? ", mirrored" : "", seen,
              ok ? "the wrong code" : "nothing read");
        if (ok) { read++; mirrored += mirror; if (m < smallest) smallest = m; }
    }
    /* a frame with nothing in it */
    memset(s_blur, 128, sizeof s_blur);
    int seen = -1;
    CHECK(!qrscan_frame(s_blur, W, H, got, sizeof got, &seen) && seen == 0, "a blank frame gave a code");
    qrscan_free();

    if (s_bad) return 1;
    printf("scancheck: %d of %d frames read (%d mirrored), modules down to %.1f px; codes found in links and refused elsewhere\n",
           read, frames, mirrored, smallest);
    return 0;
}
