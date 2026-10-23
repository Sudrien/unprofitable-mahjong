/* SPDX-License-Identifier: MIT */
/* test/fake: an in-memory NVS, see savecheck.c. */
#pragma once
#include "esp_log.h"
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h);
void nvs_close(nvs_handle_t h);
esp_err_t nvs_commit(nvs_handle_t h);
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len);
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *v, size_t len);
esp_err_t nvs_get_i32(nvs_handle_t h, const char *key, int32_t *v);
esp_err_t nvs_set_i32(nvs_handle_t h, const char *key, int32_t v);
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *v);
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t v);
esp_err_t nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *v);
esp_err_t nvs_set_u32(nvs_handle_t h, const char *key, uint32_t v);
typedef struct { size_t used_entries, free_entries, available_entries, total_entries, namespace_count; } nvs_stats_t;
static inline esp_err_t nvs_get_stats(const char *part, nvs_stats_t *st) { (void)part; (void)st; return -1; }
