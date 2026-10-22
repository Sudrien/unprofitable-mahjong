# camera/

`sc202cs_ipa.json` is the image processing tuning for the Tab5's SC202CS
sensor: esp_cam_sensor's `sensors/sc202cs/cfg/sc202cs_default.json`, as
in esp_cam_sensor 2.6.0 (esp-video-components `b9c428f`, unchanged since
`14d25f5`), with its colour-correction table made plain:

    acc.ccm.table: every color_temp's matrix -> the identity

The ISP takes colour-matrix terms within [-4, 4]. Under warm light the
default table's matrices, after white balance and saturation, came out
with a blue term from 4.26 to 4.81 on this Tab5, and each frame's was
refused:

    E ISP_CCM: esp_isp_ccm_configure(30): Matrix[2][2] value 4.813189 is out of range [-4.000000, 4.000000]

Capping the one 4.54 term at 3.99 (0068) was not enough. The camera is
only used for the grey of each frame, to read QR codes, so the colours
go uncorrected; white balance, exposure and the rest are as they were.

`sdkconfig.defaults` points the sensor at this file
(`CONFIG_CAMERA_SC202CS_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE_PATH`,
relative to the project). Espressif's ESPRESSIF MIT licence, as the
component's.
