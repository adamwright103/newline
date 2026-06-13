#ifndef PAYLOAD_STORE_H
#define PAYLOAD_STORE_H

#include <stddef.h>
#include <stdbool.h>

typedef enum
{
    PAYLOAD_STATUS_FETCHING,
    PAYLOAD_STATUS_READY,
    PAYLOAD_STATUS_FAILED
} payload_status_t;

typedef struct
{
    float precip[24];
    float uv[24];
    float temp[24];
} hourly_weather_t;

typedef struct
{
    float max_temp;
    float min_temp;
    float total_precip;
    char sunrise[8];
    char sunset[8];
    hourly_weather_t hourly;
} weather_data_t;

typedef struct
{
    char day[16];
    char formatted[32];
} date_data_t;

typedef struct
{
    char puzzle[128];
    char length[16];
} cryptic_data_t;

void payload_store_init(void);
void payload_store_set_status(payload_status_t status);
payload_status_t payload_store_get_status(void);

void payload_store_set_data(const weather_data_t *weather, const date_data_t *date, const cryptic_data_t *cryptic);

// Getters - returning bool to indicate if data is ready
bool payload_store_get_weather(weather_data_t *out_data);
bool payload_store_get_date(date_data_t *out_data);
bool payload_store_get_cryptic(cryptic_data_t *out_data);

#endif // PAYLOAD_STORE_H