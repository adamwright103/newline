#include "payload_store.h"
#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

static char s_payload[MAX_HTTP_BUF] = "";
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
    strncpy(s_payload, "{\"status\": \"Waiting for initial data...\"}", MAX_HTTP_BUF - 1);
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

void payload_store_set_data(const char *json_str)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY))
    {
        if (json_str != NULL)
        {
            strncpy(s_payload, json_str, MAX_HTTP_BUF - 1);
            s_payload[MAX_HTTP_BUF - 1] = '\0';
            s_status = PAYLOAD_STATUS_READY;
            ESP_LOGI(TAG, "Payload store updated with: %s", json_str);
        }
        else
        {
            s_status = PAYLOAD_STATUS_FAILED; // Or whatever makes sense for your app
            ESP_LOGW(TAG, "Payload store received a NULL string!");
        }
        xSemaphoreGive(s_mutex);
    }
}

void payload_store_get_response(char *out_buf, size_t max_len)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY))
    {
        switch (s_status)
        {
        case PAYLOAD_STATUS_READY:
            strncpy(out_buf, s_payload, max_len - 1);
            break;
        case PAYLOAD_STATUS_FETCHING:
            strncpy(out_buf, "{\"status\": \"fetching\"}", max_len - 1);
            break;
        case PAYLOAD_STATUS_FAILED:
            strncpy(out_buf, "{\"status\": \"fetch failed\"}", max_len - 1);
            break;
        }
        out_buf[max_len - 1] = '\0';
        xSemaphoreGive(s_mutex);
    }
}