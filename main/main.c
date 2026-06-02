#include "nvs_flash.h"
#include "esp_log.h"
#include "src/payload_store.h"
#include "src/wifi_manager.h"
#include "src/webserver.h"
#include "src/fetcher.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Thread-Safe Storage
    payload_store_init();

    // Initialize and block until connection is established
    ESP_LOGI(TAG, "Initializing Wi-Fi Radio...");
    wifi_manager_init_and_wait();

    // Spin up modules
    webserver_start();
    fetcher_start();
}