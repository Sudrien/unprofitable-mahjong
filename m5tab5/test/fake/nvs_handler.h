/* SPDX-License-Identifier: MIT */
/* test/fake: feckless_nvs_handler's API, kept in memory by savecheck.c. */
#pragma once
#include <stdbool.h>
#include "esp_log.h"
bool nvsh_ready(void);
esp_err_t nvsh_get_blob(const char *ns, const char *key, void *buf, size_t *len);
esp_err_t nvsh_set_blob(const char *ns, const char *key, const void *buf, size_t len);
esp_err_t nvsh_get_u8(const char *ns, const char *key, uint8_t *out);
esp_err_t nvsh_set_u8(const char *ns, const char *key, uint8_t v);
esp_err_t nvsh_get_u32(const char *ns, const char *key, uint32_t *out);
esp_err_t nvsh_set_u32(const char *ns, const char *key, uint32_t v);
esp_err_t nvsh_get_i32(const char *ns, const char *key, int32_t *out);
esp_err_t nvsh_set_i32(const char *ns, const char *key, int32_t v);
