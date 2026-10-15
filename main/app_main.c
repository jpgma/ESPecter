#include <stdio.h>
#include <stdlib.h>

#include "arena.h"
#include "engine_host.h"

#include "driver/gpio.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

/* [ble_start] Liga o controller BLE e solta a memória do CLASSIC_BT. */
static void ble_start(void)
{
    esp_bt_controller_config_t controller_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bluedroid_config_t host_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    esp_err_t released;

    released = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (released != ESP_OK && released != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(released);
    }
    ESP_ERROR_CHECK(esp_bt_controller_init(&controller_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init_with_cfg(&host_cfg));
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_LOGI(TAG, "ble controller up");
}

/* [app_main] Liga o BAT_EN e então [nvs_start], [ble_start], [arena_init] e [engine_host_run]. */
void app_main(void)
{
    void *block;

    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_2, 1);

    nvs_start();
    ble_start();

    block = heap_caps_malloc(ARENA_BYTES, MALLOC_CAP_SPIRAM);
    if (block == NULL || arena_init(block, ARENA_BYTES) != 0) {
        ESP_LOGE(TAG, "arena init failed");
        abort();
    }

    engine_host_run();
}
