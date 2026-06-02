#include "fetcher.h"
#include "payload_store.h"
#include "app_config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "FETCHER";

typedef struct
{
    char *buffer;
    size_t max_len;
    size_t len;
    bool truncated;
} http_response_t;

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *response = (http_response_t *)evt->user_data;
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        if (response == NULL || response->buffer == NULL)
            break;
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
    default:
        break;
    }
    return ESP_OK;
}

static bool fetch_local_data(void)
{
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
    bool success = false;

    if (err == ESP_OK)
    {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200)
        {
            if (response.truncated)
            {
                ESP_LOGE(TAG, "HTTP response exceeded %d bytes; skipping parse", MAX_HTTP_BUF - 1);
            }
            else
            {
                // cJSON_Parse performs internal stack structural validation
                cJSON *json = cJSON_Parse(local_response_buffer);
                if (json == NULL)
                {
                    const char *error_ptr = cJSON_GetErrorPtr();
                    if (error_ptr != NULL)
                    {
                        ESP_LOGE(TAG, "JSON Parsing validation failed before: %s", error_ptr);
                    }
                }
                else
                {
                    char *formatted_json = cJSON_PrintUnformatted(json);
                    if (formatted_json != NULL)
                    {
                        payload_store_set_data(formatted_json);
                        free(formatted_json);
                        success = true;
                    }
                    cJSON_Delete(json);
                }
            }
        }
        else
        {
            ESP_LOGE(TAG, "HTTP GET returned status code: %d", status_code);
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return success;
}

static void fetcher_task(void *pvParameters)
{
    while (1)
    {
        payload_store_set_status(PAYLOAD_STATUS_FETCHING);
        bool fetch_ok = false;

        for (int attempt = 1; attempt <= MAX_FETCH_RETRIES; attempt++)
        {
            ESP_LOGI(TAG, "Fetch attempt %d of %d...", attempt, MAX_FETCH_RETRIES);
            if (fetch_local_data())
            {
                fetch_ok = true;
                break;
            }
            if (attempt < MAX_FETCH_RETRIES)
            {
                vTaskDelay(pdMS_TO_TICKS(FETCH_RETRY_DELAY_MS));
            }
        }

        if (!fetch_ok)
        {
            ESP_LOGE(TAG, "All %d fetch attempts failed.", MAX_FETCH_RETRIES);
            payload_store_set_status(PAYLOAD_STATUS_FAILED);
        }

        vTaskDelay(pdMS_TO_TICKS(FETCH_INTERVAL_MS));
    }
}

void fetcher_start(void)
{
    xTaskCreate(&fetcher_task, "fetcher_task", 4096, NULL, 5, NULL);
}