#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "../secret.h"

#define LOCAL_URL "http://192.168.1.178:8080/"
#define MAX_HTTP_BUF 1024

static const char *TAG = "NEWLINE";

// FreeRTOS Event Group to signal when connected to WiFi
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

// Shared payload and mutex to protect it from simultaneous read/writes
static char eink_payload[MAX_HTTP_BUF] = "{\"status\": \"Waiting for initial data...\"}";
static SemaphoreHandle_t payload_mutex;

// ---------------------------------------------------------
// HTTP SERVER HANDLER
// ---------------------------------------------------------
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    // Lock mutex before reading the shared payload
    if (xSemaphoreTake(payload_mutex, portMAX_DELAY))
    {
        httpd_resp_send(req, eink_payload, HTTPD_RESP_USE_STRLEN);
        xSemaphoreGive(payload_mutex);
    }
    else
    {
        httpd_resp_send_500(req);
    }
    return ESP_OK;
}

static void start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &root);
    }
}

// ---------------------------------------------------------
// HTTP CLIENT HANDLER
// ---------------------------------------------------------
typedef struct
{
    char *buffer;
    size_t max_len;
    size_t len;
    bool truncated;
} http_response_t;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *response = (http_response_t *)evt->user_data;

    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
    {
        if (response == NULL || response->buffer == NULL)
        {
            break;
        }

        size_t available = (response->max_len - 1) - response->len;
        size_t copy_len = evt->data_len < available ? evt->data_len : available;

        if (copy_len > 0)
        {
            memcpy(response->buffer + response->len, evt->data, copy_len);
            response->len += copy_len;
            response->buffer[response->len] = '\0';
        }
        if (evt->data_len > copy_len)
        {
            response->truncated = true;
        }
        break;
    }
    default:
        break;
    }
    return ESP_OK;
}

static void fetch_local_data(void)
{
    ESP_LOGI(TAG, "Fetching fresh data from local server...");

    char local_response_buffer[MAX_HTTP_BUF] = {0};

    http_response_t response = {
        .buffer = local_response_buffer,
        .max_len = MAX_HTTP_BUF,
        .len = 0,
        .truncated = false,
    };

    esp_http_client_config_t config = {
        .url = LOCAL_URL,
        .event_handler = _http_event_handler,
        .user_data = &response,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));

        if (esp_http_client_get_status_code(client) == 200)
        {
            if (response.truncated)
            {
                ESP_LOGE(TAG, "HTTP response exceeded %d bytes; skipping parse", MAX_HTTP_BUF - 1);
                esp_http_client_cleanup(client);
                return;
            }

            ESP_LOGI(TAG, "Data received. Parsing...");

            // Print the raw string returned from the external server as requested
            ESP_LOGI(TAG, "Raw Payload from server:\n%s", local_response_buffer);

            cJSON *json = cJSON_Parse(local_response_buffer);
            if (json == NULL)
            {
                const char *error_ptr = cJSON_GetErrorPtr();
                if (error_ptr != NULL)
                {
                    ESP_LOGE(TAG, "JSON Parsing failed before: %s", error_ptr);
                }
            }
            else
            {
                // Formatting payload back to string
                char *formatted_json = cJSON_PrintUnformatted(json);

                // Thread-safe update of the payload
                if (xSemaphoreTake(payload_mutex, portMAX_DELAY))
                {
                    strncpy(eink_payload, formatted_json, MAX_HTTP_BUF - 1);
                    eink_payload[MAX_HTTP_BUF - 1] = '\0';
                    xSemaphoreGive(payload_mutex);
                }

                ESP_LOGI(TAG, "Successfully updated eInk payload.");

                free(formatted_json); // Free buffer allocated by cJSON_Print
                cJSON_Delete(json);   // Free JSON object memory
            }
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

// Fetcher Task: Runs periodically (every 3 hours)
static void fetcher_task(void *pvParameters)
{
    // 3 hours in milliseconds
    const TickType_t delay_ticks = (3 * 60 * 60 * 1000) / portTICK_PERIOD_MS;

    while (1)
    {
        fetch_local_data();
        vTaskDelay(delay_ticks);
    }
}

// ---------------------------------------------------------
// WIFI EVENT HANDLER
// ---------------------------------------------------------
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Connection failed. Retrying...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "ESP32 Local Network IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // FORCE ESP32 REGULATORY DOMAIN TO NEW ZEALAND
    wifi_country_t nz_country = {
        .cc = "NZ",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_MANUAL};
    ESP_ERROR_CHECK(esp_wifi_set_country(&nz_country));
    ESP_LOGI(TAG, "Country domain set to NZ. Channels 12 and 13 unlocked.");

    ESP_LOGI(TAG, "Attempting connection to: %s", WIFI_SSID);

    // Wait until connection is established
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "Connected successfully!");
}

// ---------------------------------------------------------
// MAIN APPLICATION
// ---------------------------------------------------------
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

    // Initialize Mutex
    payload_mutex = xSemaphoreCreateMutex();

    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing Wi-Fi Radio...");
    wifi_init_sta();

    start_webserver();

    xTaskCreate(&fetcher_task, "fetcher_task", 4096, NULL, 5, NULL);
}