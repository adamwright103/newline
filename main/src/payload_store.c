#include "payload_store.h"
#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

// Define three separate struct stores
static weather_data_t s_weather;
static date_data_t s_date;
static cryptic_data_t s_cryptic;

static const char *TAG = "PAYLOAD_STORE";
static payload_status_t s_status = PAYLOAD_STATUS_FETCHING;
static SemaphoreHandle_t s_mutex = NULL;

void payload_store_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create mutex!");
        return;
    }

    // Zero out memory initially
    memset(&s_weather, 0, sizeof(weather_data_t));
    memset(&s_date, 0, sizeof(date_data_t));
    memset(&s_cryptic, 0, sizeof(cryptic_data_t));
}

void payload_store_set_status(payload_status_t status)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY))
    {
        s_status = status;
        xSemaphoreGive(s_mutex);
    }
}

payload_status_t payload_store_get_status(void)
{
    payload_status_t status = PAYLOAD_STATUS_FETCHING;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY))
    {
        status = s_status;
        xSemaphoreGive(s_mutex);
    }
    return status;
}

void payload_store_set_data(const weather_data_t *weather, const date_data_t *date, const cryptic_data_t *cryptic)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY))
    {
        if (weather != NULL && date != NULL && cryptic != NULL)
        {
            s_weather = *weather;
            s_date = *date;
            s_cryptic = *cryptic;

            s_status = PAYLOAD_STATUS_READY;
            ESP_LOGI(TAG, "Payload store updated successfully.");
        }
        else
        {
            s_status = PAYLOAD_STATUS_FAILED;
            ESP_LOGW(TAG, "Payload store received a NULL struct pointer!");
        }
        xSemaphoreGive(s_mutex);
    }
}

bool payload_store_get_weather(weather_data_t *out_data)
{
    bool is_ready = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY))
    {
        if (s_status == PAYLOAD_STATUS_READY && out_data != NULL)
        {
            *out_data = s_weather;
            is_ready = true;
        }
        xSemaphoreGive(s_mutex);
    }
    return is_ready;
}

bool payload_store_get_date(date_data_t *out_data)
{
    bool is_ready = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY))
    {
        if (s_status == PAYLOAD_STATUS_READY && out_data != NULL)
        {
            *out_data = s_date;
            is_ready = true;
        }
        xSemaphoreGive(s_mutex);
    }
    return is_ready;
}

bool payload_store_get_cryptic(cryptic_data_t *out_data)
{
    bool is_ready = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY))
    {
        if (s_status == PAYLOAD_STATUS_READY && out_data != NULL)
        {
            *out_data = s_cryptic;
            is_ready = true;
        }
        xSemaphoreGive(s_mutex);
    }
    return is_ready;
}