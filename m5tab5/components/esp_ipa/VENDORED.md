# esp_ipa, vendored

Espressif's image process algorithms for the ISP, from
[esp-video-components](https://github.com/espressif/esp-video-components/tree/master/esp_ipa)
at commit `3ae2756e21c5f85ed8d9809100ab02d079347a79` (2026-10-21),
whose libraries are build `a68f3ec`. ESPRESSIF MIT licence (LICENSE).

**Why it is here.** The registry's esp_ipa 2.4.0, which esp_video 2.5.0
pulls in, links one prebuilt library whatever the chip, and that library
uses Zba instructions (sh2add and the like) that ESP32-P4 silicon before
v3 does not have. On this Tab5 (v1.3) the first camera_init() died with
an illegal instruction in create_ipa_array. Upstream fixed it on
2026-09-20 (5ec4d12, "Support esp-idf v6.0+ for ESP32-P4 less v3 chip"):
CONFIG_ESP32P4_SELECTS_REV_LESS_V3 picks libesp_ipa_p4_less_v3.a. That
is not in a registry release yet, and a component in the project's
components/ wins over the managed one of the same name.

**What was kept.** The component as it is, but under lib/ only the
less-than-v3 libraries (and each directory's version file): this
project builds with CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y. A build for
v3 silicon would look for libesp_ipa.a and fail to link, which is the
safe way to fail.

**When to drop it.** Once a registry esp_ipa newer than 2.4.0 has the
less-than-v3 libraries and esp_video asks for it: delete this folder and
read the dependencies.lock diff.
