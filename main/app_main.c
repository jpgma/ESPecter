#include <stdio.h>
#include <stdlib.h>

#include "arena.h"
#include "battery.h"
#include "engine_host.h"
#include "pad_link.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "especter";

/* [nvs_start] Sobe o NVS e apaga a partition se a versão não bater. */
static void nvs_start(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/* [app_main] Liga o BAT_EN e então [arena_internal_init], [battery_start], [nvs_start], [pad_link_start], [arena_init] e [engine_host_run]. */
void app_main(void)
{
    void *internal;
    void *block;

    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_2, 1);
    internal = heap_caps_malloc(ARENA_INTERNAL_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (internal == NULL || arena_internal_init(internal, ARENA_INTERNAL_BYTES) != 0) {
        ESP_LOGE(TAG, "internal arena init failed");
        abort();
    }
    battery_start();

    nvs_start();
    pad_link_start();

    block = heap_caps_malloc(ARENA_BYTES, MALLOC_CAP_SPIRAM);
    if (block == NULL || arena_init(block, ARENA_BYTES) != 0) {
        ESP_LOGE(TAG, "arena init failed");
        abort();
    }

    engine_host_run();
}
