/* SPDX-License-Identifier: MIT
 *
 * u-unprofit-0001: what a box does not answer, as the game's own functions.
 *
 * The boxed ELF is the game's sources as they were, so it still calls the
 * camera (Scan a code) and two of gfx's calls that only the camera's view
 * uses. A feckless box (feckless-libs' umode branch, to u-feckless-0058)
 * has no host call for any of them, and an import it does not answer
 * fails the load by name. Each is defined here to fail the way the game
 * already copes with on a Tab5 whose camera does not answer -- the shape
 * is Defeatist's boxed/main/absent.c:
 *
 *   the camera   never there: camera_init() fails, which main.c logs and
 *                carries on from; camera_present() is false, so Scan a
 *                code says "The camera is not available." and closes on a
 *                tap. Nothing is scanned in a box, so the loading secrets
 *                (Guaranteed Random, Try Try Again, Shadows of the Past)
 *                cannot be earned in one. Ones earned before are kept.
 *   gfx_px       one pixel, as gfx_fill_rect() draws it.
 *   gfx_rotation only the camera's view asks, to stand its frame up, and
 *                that view never opens here: the panel's own angle.
 *
 * Each comes out of this file when a box answers it -- the box's call
 * then replaces it at the next build, and feckless_abi_require_camera()
 * goes back into main.c with the first of the camera's. Not a stub of the
 * game's own code: every name here is a library's.
 */
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "camera.h"
#include "gfx.h"

esp_err_t camera_init(i2c_master_bus_handle_t bus)
{
    (void)bus;
    return ESP_ERR_NOT_SUPPORTED;
}

bool camera_present(void) { return false; }

esp_err_t camera_start(void) { return ESP_ERR_NOT_SUPPORTED; }

void camera_luma_size(int step, int *w, int *h)
{
    (void)step;
    if (w) *w = 0;
    if (h) *h = 0;
}

esp_err_t camera_grab_luma(uint8_t *dst, int step, int *w, int *h)
{
    (void)dst;
    (void)step;
    (void)w;
    (void)h;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t camera_stop(void) { return ESP_OK; }

void gfx_px(int x, int y, uint16_t c) { gfx_fill_rect(x, y, 1, 1, c); }

int gfx_rotation(void) { return GFX_ROT_0; }
