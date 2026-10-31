/* SPDX-License-Identifier: MIT
 *
 * The .bin's app_main(). The game is the ELF that ../elf/ built from
 * main.c and the rest, embedded in this firmware, and this loads it the
 * way a launcher would load the published copy: through the feckless
 * ABI, with its nvs and camera sections (feckless_abi_nvs,
 * feckless_abi_camera), which this firmware links.
 *
 * NVS comes up here, as a launcher brings it up: the game finds it up.
 */
#include <stdint.h>

#include "esp_log.h"
#include "feckless_abi.h"
#include "nvs_handler.h"

extern const uint8_t elf_start[] asm("_binary_unprofitable_mahjong_app_elf_start");
extern const uint8_t elf_end[]   asm("_binary_unprofitable_mahjong_app_elf_end");

void app_main(void)
{
    nvsh_init();    /* a failure is logged; the game plays and saves nothing */
    int ret = feckless_abi_run(elf_start, (size_t)(elf_end - elf_start));
    ESP_LOGE("standalone", "the game did not start: %d", ret);
}
