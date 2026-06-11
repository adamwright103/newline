#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// --- APP INCLUDES ---
#include "src/payload_store.h"
#include "src/wifi_manager.h"
#include "src/fetcher.h"
#include "src/app_config.h"
#include "src/display.h"

#define DEBUG_MODE 1
static const char *TAG = "MAIN";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    payload_store_init();
    display_init();
    wifi_manager_init_and_wait();

    ESP_LOGI(TAG, "Waiting for network to settle...");
    vTaskDelay(pdMS_TO_TICKS(2500));

    bool success = fetch_daily_info();
    if (success)
    {
        ESP_LOGI(TAG, "Data fetched successfully!");

        // Handle Display Updates
        display_draw_static_ui();
    }
    else
    {
        ESP_LOGE(TAG, "Data fetch completely failed this round.");
    }

#if DEBUG_MODE
    uint32_t total_minutes = FETCH_INTERVAL_MS_DEBUG / 60000;
    uint64_t sleep_time_us = (uint64_t)FETCH_INTERVAL_MS_DEBUG * 1000;
#else
    uint32_t total_minutes = FETCH_INTERVAL_MS / 60000;
    uint64_t sleep_time_us = (uint64_t)FETCH_INTERVAL_MS * 1000;
#endif
    uint32_t hours = total_minutes / 60;
    uint32_t minutes = total_minutes % 60;

    ESP_LOGI(TAG, "Entering deep sleep for %u hours and %u minutes...", hours, minutes);

    esp_wifi_disconnect();
    esp_wifi_stop();
    display_deinit();

    esp_deep_sleep(sleep_time_us);
}