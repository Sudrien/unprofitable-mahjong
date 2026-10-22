# quirc

Daniel Beer's QR-code recognition library, copied unchanged from
https://github.com/dlbeer/quirc at tag `v1.2`
(commit `542848dd6b9b0eaa9587bbf25b9bc67bd8a71fca`), the files under `lib/`.

ISC licensed; the licence is in LICENSE. It finds and decodes the QR
code of a replay link in a camera frame on the Tab5 (`main/qrscan.c`).

Built with `QUIRC_FLOAT_TYPE=float` (and `QUIRC_USE_TGMATH`), since the
ESP32-P4's FPU is single precision. Its buffers come from malloc, which
puts anything this size in PSRAM.
