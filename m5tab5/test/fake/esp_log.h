/* SPDX-License-Identifier: MIT */
/* test/fake: just enough of ESP-IDF for main/save.c on the host. */
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK                         (0)
#define ESP_ERR_NVS_NOT_FOUND          (0x1102)
#define ESP_ERR_NVS_NO_FREE_PAGES      (0x110d)
#define ESP_ERR_NVS_NEW_VERSION_FOUND  (0x1110)
#define ESP_ERR_NVS_INVALID_LENGTH     (0x110c)
const char *esp_err_to_name(esp_err_t e);
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
