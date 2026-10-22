# feckless_camera

The M5Stack Tab5's camera, as grey frames. A local component while it
settles; it leaves for its own repository (beside
[feckless-drivers](https://github.com/Sudrien/feckless-drivers-for-m5tab5))
once it has run on a board.

**Not yet run on a Tab5.** Written from esp_video 2.5.0 and Espressif's
`m5stack_tab5` BSP.

| | |
|---|---|
| Sensor | SmartSens SC202CS (SC2356), MIPI CSI, 1 lane |
| Mode | 1280x720 RAW8 30 fps (Kconfig default), ISP to UYVY |
| Bus | SCCB on tab5io's I2C, 400 kHz |
| Reset | CAM_RST, expander 0x43 P6, released by `tab5io_init()` |
| PHY supply | LDO 3 at 2.5 V, held by `lcd_init()` |

## Using it

```c
tab5io_init();
lcd_init(&panel);
if (camera_init(tab5io_bus()) == ESP_OK) {
    camera_start();
    int w, h;
    camera_luma_size(2, &w, &h);                 /* 640x360 */
    uint8_t *y = heap_caps_malloc(w * h, MALLOC_CAP_SPIRAM);
    camera_grab_luma(y, 2, NULL, NULL);
    camera_stop();
}
```

`feckless_camera` in `main`'s `REQUIRES`, and in `sdkconfig.defaults`:

```
CONFIG_CAMERA_SC202CS=y
CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=y
```

The second is what gives the ISP auto exposure and white balance;
without it the picture is near black.

## Why not in feckless_drivers

esp_video pulls esp_cam_sensor, esp_ipa, esp_h264 and usb_host_uvc.
Everything that uses feckless_drivers and never opens a camera would
build all of it. The only thing the camera needs from the drivers is
the bus, so the dependency runs one way.
