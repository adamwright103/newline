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
    char *local_response_buffer = malloc(MAX_HTTP_BUF);
    if (local_response_buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for HTTP response buffer!");
        return false;
    }
    memset(local_response_buffer, 0, MAX_HTTP_BUF);

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
                    payload_store_set_data(local_response_buffer);
                    cJSON_Delete(json);
                    success = true;
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
    free(local_response_buffer);
    return success;
}

bool fetch_daily_info(void)
{
    payload_store_set_status(PAYLOAD_STATUS_FETCHING);

    for (int attempt = 1; attempt <= MAX_FETCH_RETRIES; attempt++)
    {
        ESP_LOGI(TAG, "Fetch attempt %d of %d...", attempt, MAX_FETCH_RETRIES);
        if (fetch_local_data())
        {
            return true;
        }
        if (attempt < MAX_FETCH_RETRIES)
        {
            vTaskDelay(pdMS_TO_TICKS(FETCH_RETRY_DELAY_MS));
        }
    }

    payload_store_set_status(PAYLOAD_STATUS_FAILED);
    return false;
}