# camera/

`sc202cs_ipa.json` is the image processing tuning for the Tab5's SC202CS
sensor: esp_cam_sensor's `sensors/sc202cs/cfg/sc202cs_default.json`, as
in esp_cam_sensor 2.6.0 (esp-video-components `b9c428f`, unchanged since
`14d25f5`), with one value changed:

    acc.ccm.table, color_temp 2292: matrix[8] (row 2, column 2)  4.5445 -> 3.99

The ISP takes colour-matrix terms within [-4, 4]. Under warm light the
matrix interpolated toward the 2292 K entry crossed 4.0 (4.26 to 4.40 on
this Tab5), and each frame's was refused:

    E ISP_CCM: esp_isp_ccm_configure(30): Matrix[2][2] value 4.317374 is out of range [-4.000000, 4.000000]

`sdkconfig.defaults` points the sensor at this file
(`CONFIG_CAMERA_SC202CS_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE_PATH`,
relative to the project). Espressif's ESPRESSIF MIT licence, as the
component's.
