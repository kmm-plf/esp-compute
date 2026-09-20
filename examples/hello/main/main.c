#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"

#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif

static const char *TAG = "hello";

void app_main(void)
{
    esp_chip_info_t info;
    esp_chip_info(&info);

    ESP_LOGI(TAG, "=== ESP32 board self-check ===");
    ESP_LOGI(TAG, "chip       : %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "cores      : %d", info.cores);
    ESP_LOGI(TAG, "revision   : %d", info.revision);
    ESP_LOGI(TAG, "wifi       : %d", (info.features & CHIP_FEATURE_WIFI_BGN) ? 1 : 0);
    ESP_LOGI(TAG, "ble        : %d", (info.features & CHIP_FEATURE_BLE) ? 1 : 0);

    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "flash      : %" PRIu32 " MB", flash_size / (1024U * 1024U));
    }

#if CONFIG_SPIRAM
    ESP_LOGI(TAG, "psram      : %u bytes", (unsigned)esp_psram_get_size());
#else
    ESP_LOGI(TAG, "psram      : disabled");
#endif

    ESP_LOGI(TAG, "free heap  : %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "idf version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "=== self-check done ===");
}
