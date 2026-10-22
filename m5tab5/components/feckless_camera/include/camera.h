/*
 * camera.h -- the Tab5's camera, as luma frames.
 *
 * The sensor is a SmartSens SC202CS (sold as SC2356) on MIPI CSI, one
 * lane, 1280x720 RAW8 at 30fps; the P4's ISP debayers it to UYVY and
 * this hands out the Y. Grey is all a QR decoder wants.
 *
 * Its own component so feckless_drivers does not carry esp_video for
 * everything that never opens a camera. It takes the shared I2C bus
 * from tab5io and nothing else: tab5io_init() already releases CAM_RST
 * (expander 0x43, P6), and lcd_init() already holds the MIPI PHY's LDO,
 * which the CSI side shares with DSI.
 *
 * Order:
 *
 *   tab5io_init();
 *   lcd_init(&panel);               // the PHY LDO
 *   camera_init(tab5io_bus());      // probe the sensor; nothing streams
 *   ...
 *   camera_start();                 // stream on
 *   camera_grab_luma(buf, 2, &w, &h);   // as often as wanted
 *   camera_stop();                  // stream off, buffers freed
 *
 * Not thread-safe: one task owns the camera between start and stop.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Probe the sensor on the shared bus and register /dev/video0. Once per
 * boot; a second call returns ESP_OK and does nothing. Returns
 * esp_video's error when no sensor answers, which the caller should
 * treat as "no camera", not as a reason to stop booting. */
esp_err_t camera_init(i2c_master_bus_handle_t bus);

/* True once camera_init() has found a sensor. */
bool camera_present(void);

/* Open the device, ask for UYVY, map the buffers and start streaming. */
esp_err_t camera_start(void);

/* The size camera_grab_luma() writes at a given step (1 = full frame,
 * 2 = every other pixel of every other line, ...). Valid after
 * camera_start(). */
void camera_luma_size(int step, int *w, int *h);

/* Wait for the next frame and copy its luma into dst, taking every
 * step-th pixel of every step-th line. dst holds at least w*h bytes as
 * camera_luma_size() gives them; put it in PSRAM -- a full frame is
 * 900 KiB. w and h may be NULL. Blocks until a frame arrives, usually
 * under a frame time; ESP_ERR_TIMEOUT after half a second without one. */
esp_err_t camera_grab_luma(uint8_t *dst, int step, int *w, int *h);

/* Stop streaming and release the buffers and the device. Safe to call
 * when not started. */
esp_err_t camera_stop(void);

#ifdef __cplusplus
}
#endif
