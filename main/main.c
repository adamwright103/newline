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

        weather_data_t weather;
        if (payload_store_get_weather(&weather))
        {
            ESP_LOGI(TAG, "--- WEATHER PAYLOAD ---");
            ESP_LOGI(TAG, "Max Temp: %.1f, Min Temp: %.1f, Precip: %.1f",
                     weather.max_temp, weather.min_temp, weather.total_precip);
            ESP_LOGI(TAG, "Sunrise: %s, Sunset: %s", weather.sunrise, weather.sunset);

            // Optional: Print the 24-hour temperature array to verify
            char array_buf[128] = {0};
            int offset = 0;
            for (int i = 0; i < 24; i++)
            {
                offset += snprintf(array_buf + offset, sizeof(array_buf) - offset, "%.1f%s",
                                   weather.hourly.temp[i], (i == 23) ? "" : ", ");
            }
            ESP_LOGI(TAG, "Hourly Temps: [%s]", array_buf);
        }

        date_data_t date;
        if (payload_store_get_date(&date))
        {
            ESP_LOGI(TAG, "--- DATE PAYLOAD ---");
            ESP_LOGI(TAG, "Day: %s", date.day);
            ESP_LOGI(TAG, "Formatted: %s", date.formatted);
        }

        cryptic_data_t cryptic;
        if (payload_store_get_cryptic(&cryptic))
        {
            ESP_LOGI(TAG, "--- CRYPTIC PAYLOAD ---");
            ESP_LOGI(TAG, "Puzzle: %s", cryptic.puzzle);
            ESP_LOGI(TAG, "Length: %s", cryptic.length);
        }

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