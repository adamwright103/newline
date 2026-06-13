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

/* ================== HELPER DESTRUCTURING FUNCTIONS ================== */

static void parse_float_array(cJSON *array_item, float *out_array, size_t max_items)
{
    if (cJSON_IsArray(array_item))
    {
        int size = cJSON_GetArraySize(array_item);
        for (int i = 0; i < size && i < max_items; i++)
        {
            cJSON *item = cJSON_GetArrayItem(array_item, i);
            out_array[i] = cJSON_IsNumber(item) ? item->valuedouble : 0.0f;
        }
    }
}

static bool parse_weather_data(cJSON *weather_item, weather_data_t *weather)
{
    if (!weather_item)
        return false;

    cJSON *max_temp = cJSON_GetObjectItem(weather_item, "max_temp");
    cJSON *min_temp = cJSON_GetObjectItem(weather_item, "min_temp");
    cJSON *total_precip = cJSON_GetObjectItem(weather_item, "total_precip");
    cJSON *sunrise = cJSON_GetObjectItem(weather_item, "sunrise");
    cJSON *sunset = cJSON_GetObjectItem(weather_item, "sunset");
    cJSON *hourly = cJSON_GetObjectItem(weather_item, "hourly");

    if (cJSON_IsNumber(max_temp))
        weather->max_temp = max_temp->valuedouble;
    if (cJSON_IsNumber(min_temp))
        weather->min_temp = min_temp->valuedouble;
    if (cJSON_IsNumber(total_precip))
        weather->total_precip = total_precip->valuedouble;

    if (cJSON_IsString(sunrise))
        strncpy(weather->sunrise, sunrise->valuestring, sizeof(weather->sunrise) - 1);
    if (cJSON_IsString(sunset))
        strncpy(weather->sunset, sunset->valuestring, sizeof(weather->sunset) - 1);

    if (hourly)
    {
        // Now targeting the nested .hourly fields
        parse_float_array(cJSON_GetObjectItem(hourly, "precip"), weather->hourly.precip, 24);
        parse_float_array(cJSON_GetObjectItem(hourly, "uv"), weather->hourly.uv, 24);
        parse_float_array(cJSON_GetObjectItem(hourly, "temp"), weather->hourly.temp, 24);
    }
    return true;
}

static bool parse_date_data(cJSON *date_item, date_data_t *date)
{
    if (!date_item)
        return false;

    cJSON *day = cJSON_GetObjectItem(date_item, "day");
    cJSON *formatted = cJSON_GetObjectItem(date_item, "formatted");

    if (cJSON_IsString(day))
        strncpy(date->day, day->valuestring, sizeof(date->day) - 1);
    if (cJSON_IsString(formatted))
        strncpy(date->formatted, formatted->valuestring, sizeof(date->formatted) - 1);

    return true;
}

static bool parse_cryptic_data(cJSON *cryptic_item, cryptic_data_t *cryptic)
{
    if (!cryptic_item)
        return false;

    cJSON *puzzle = cJSON_GetObjectItem(cryptic_item, "puzzle");
    cJSON *length = cJSON_GetObjectItem(cryptic_item, "length");

    if (cJSON_IsString(puzzle))
        strncpy(cryptic->puzzle, puzzle->valuestring, sizeof(cryptic->puzzle) - 1);
    if (cJSON_IsString(length))
        strncpy(cryptic->length, length->valuestring, sizeof(cryptic->length) - 1);

    return true;
}

/* ==================================================================== */

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
                    weather_data_t weather_data = {0};
                    date_data_t date_data = {0};
                    cryptic_data_t cryptic_data = {0};

                    bool w_ok = parse_weather_data(cJSON_GetObjectItem(json, "weather"), &weather_data);
                    bool d_ok = parse_date_data(cJSON_GetObjectItem(json, "date"), &date_data);
                    bool c_ok = parse_cryptic_data(cJSON_GetObjectItem(json, "cryptic"), &cryptic_data);

                    if (w_ok && d_ok && c_ok)
                    {
                        payload_store_set_data(&weather_data, &date_data, &cryptic_data);
                        success = true;
                    }
                    else
                    {
                        ESP_LOGE(TAG, "Failed to extract all required fields from JSON");
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