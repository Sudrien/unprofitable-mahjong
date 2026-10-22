/* camera.h on the host: no camera, so Scan a code says so. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
static inline bool camera_present(void) { return false; }
static inline esp_err_t camera_start(void) { return ESP_FAIL; }
static inline void camera_luma_size(int step, int *w, int *h) { (void)step; if (w) *w = 0; if (h) *h = 0; }
static inline esp_err_t camera_grab_luma(uint8_t *dst, int step, int *w, int *h) { (void)dst; (void)step; (void)w; (void)h; return ESP_FAIL; }
static inline esp_err_t camera_stop(void) { return ESP_OK; }
