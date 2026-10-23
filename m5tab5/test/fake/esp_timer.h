/* SPDX-License-Identifier: MIT */
/* test/fake: a clock that never moves, for main/save.c's timings. */
#pragma once
#include <stdint.h>
static inline int64_t esp_timer_get_time(void) { return 0; }
