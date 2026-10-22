/*
 * camera.c -- see camera.h.
 *
 * esp_video does the work: it probes the sensor over SCCB, sets up the
 * CSI controller and the ISP, and presents /dev/video0 as a V4L2
 * capture device. This file is the V4L2 dance and the luma copy.
 *
 * NOT YET RUN ON A TAB5. Written from esp_video 2.5.0's headers and
 * Espressif's m5stack_tab5 BSP (bsp_camera.c and the display_camera_video
 * example's sdkconfig.bsp.m5stack_tab5), which is where the choices
 * below come from:
 *
 *   - the sensor is the SC202CS (CONFIG_CAMERA_SC202CS), no XCLK pin
 *     (it has its own clock), no reset pin the P4 drives (CAM_RST is on
 *     the expander, released by tab5io_init()), no power-down pin;
 *   - SCCB on the bus tab5io already made, at 400 kHz;
 *   - CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER, without which the
 *     ISP runs with no auto exposure and the picture is near black.
 *
 * UYVY, not GREY: GREY on this device is the ISP's RAW8 passed through,
 * i.e. undebayered Bayer, not luma. UYVY is supported on IDF 5.x and 6.x
 * alike; the Y is every second byte, starting at the second.
 *
 * dont_init_ldo: the CSI and DSI PHYs share one supply, LDO channel 3 at
 * 2.5 V, which feckless_graphics_handler's lcd_init() has already
 * acquired. Asking for it twice is at best a second reference; it is
 * left to the panel, which is always up first.
 *
 * SPDX-License-Identifier: MIT
 */
#include "camera.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "esp_video_ioctl.h"
#include "linux/videodev2.h"

static const char *TAG = "camera";

#define SCCB_FREQ_HZ    (400000)
#define NBUF            (2)         /* one filling while one is read */
#define DQ_TIMEOUT_MS   (500)       /* 15 frame times: a stall, not a slow frame */

static bool s_present;
static int s_fd = -1;
static uint8_t *s_buf[NBUF];
static size_t s_len[NBUF];
static int s_w, s_h;
static uint32_t s_stride;           /* bytes per line, from G_FMT */

esp_err_t camera_init(i2c_master_bus_handle_t bus)
{
    if (s_present) return ESP_OK;
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_ARG, TAG, "no bus");

    /* static: esp_video keeps the pointers. */
    static esp_video_init_csi_config_t csi;
    csi = (esp_video_init_csi_config_t) {
        .sccb_config = {
            .init_sccb = false,
            .i2c_handle = bus,
            .freq = SCCB_FREQ_HZ,
        },
        .reset_pin = -1,
        .pwdn_pin = -1,
        .dont_init_ldo = true,
    };
    static esp_video_init_config_t cfg;
    cfg = (esp_video_init_config_t) { .csi = &csi };

    esp_err_t err = esp_video_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no camera (%s)", esp_err_to_name(err));
        return err;
    }
    s_present = true;
    ESP_LOGI(TAG, "sensor found, %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
    return ESP_OK;
}

bool camera_present(void) { return s_present; }

static esp_err_t start_fail(const char *what)
{
    ESP_LOGE(TAG, "%s: errno %d", what, errno);
    camera_stop();
    return ESP_FAIL;
}

esp_err_t camera_start(void)
{
    ESP_RETURN_ON_FALSE(s_present, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    if (s_fd >= 0) return ESP_OK;

    s_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (s_fd < 0) return start_fail("open");

    /* The sensor's size, as Kconfig chose it; only the format changes. */
    struct v4l2_format fmt = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
    if (ioctl(s_fd, VIDIOC_G_FMT, &fmt) != 0) return start_fail("G_FMT");
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_UYVY) {
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
        if (ioctl(s_fd, VIDIOC_S_FMT, &fmt) != 0) return start_fail("S_FMT UYVY");
        if (ioctl(s_fd, VIDIOC_G_FMT, &fmt) != 0) return start_fail("G_FMT again");
        if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_UYVY) return start_fail("UYVY refused");
    }
    s_w = (int)fmt.fmt.pix.width;
    s_h = (int)fmt.fmt.pix.height;
    s_stride = fmt.fmt.pix.bytesperline ? fmt.fmt.pix.bytesperline : (uint32_t)s_w * 2u;
    ESP_LOGI(TAG, "%dx%d UYVY, %u bytes a line", s_w, s_h, (unsigned)s_stride);

    struct timeval to = { .tv_sec = 0, .tv_usec = DQ_TIMEOUT_MS * 1000 };
    if (ioctl(s_fd, VIDIOC_S_DQBUF_TIMEOUT, &to) != 0) {
        ESP_LOGW(TAG, "no dequeue timeout: a stalled stream will block");
    }

    /* MMAP: esp_video allocates the buffers where the CSI DMA can reach
     * them, which is PSRAM at this size. */
    struct v4l2_requestbuffers req = {
        .count = NBUF,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    if (ioctl(s_fd, VIDIOC_REQBUFS, &req) != 0) return start_fail("REQBUFS");
    for (int i = 0; i < NBUF; i++) {
        struct v4l2_buffer b = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index = (uint32_t)i,
        };
        if (ioctl(s_fd, VIDIOC_QUERYBUF, &b) != 0) return start_fail("QUERYBUF");
        s_buf[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, b.m.offset);
        if (s_buf[i] == MAP_FAILED || s_buf[i] == NULL) {
            s_buf[i] = NULL;
            return start_fail("mmap");
        }
        s_len[i] = b.length;
        if (ioctl(s_fd, VIDIOC_QBUF, &b) != 0) return start_fail("QBUF");
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_fd, VIDIOC_STREAMON, &type) != 0) return start_fail("STREAMON");
    return ESP_OK;
}

void camera_luma_size(int step, int *w, int *h)
{
    if (step < 1) step = 1;
    if (w) *w = (s_w + step - 1) / step;
    if (h) *h = (s_h + step - 1) / step;
}

esp_err_t camera_grab_luma(uint8_t *dst, int step, int *w, int *h)
{
    ESP_RETURN_ON_FALSE(s_fd >= 0, ESP_ERR_INVALID_STATE, TAG, "not started");
    ESP_RETURN_ON_FALSE(dst, ESP_ERR_INVALID_ARG, TAG, "no buffer");
    if (step < 1) step = 1;

    struct v4l2_buffer b = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    if (ioctl(s_fd, VIDIOC_DQBUF, &b) != 0) {
        ESP_LOGW(TAG, "DQBUF: errno %d", errno);
        return ESP_ERR_TIMEOUT;
    }

    int ow, oh;
    camera_luma_size(step, &ow, &oh);
    const uint8_t *src = s_buf[b.index];
    /* UYVY: U0 Y0 V0 Y1 -- pixel x's Y is at 2x+1. */
    for (int y = 0; y < oh; y++) {
        const uint8_t *line = src + (size_t)(y * step) * s_stride + 1;
        uint8_t *out = dst + (size_t)y * (size_t)ow;
        for (int x = 0; x < ow; x++) out[x] = line[(size_t)(x * step) * 2u];
    }

    if (ioctl(s_fd, VIDIOC_QBUF, &b) != 0) {
        /* The frame is good; the next grab will find one fewer buffer. */
        ESP_LOGW(TAG, "QBUF: errno %d", errno);
    }
    if (w) *w = ow;
    if (h) *h = oh;
    return ESP_OK;
}

esp_err_t camera_stop(void)
{
    if (s_fd < 0) return ESP_OK;
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(s_fd, VIDIOC_STREAMOFF, &type);   /* fails harmlessly if never on */
    for (int i = 0; i < NBUF; i++) {
        if (s_buf[i]) munmap(s_buf[i], s_len[i]);
        s_buf[i] = NULL;
        s_len[i] = 0;
    }
    close(s_fd);
    s_fd = -1;
    return ESP_OK;
}
