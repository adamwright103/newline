#ifndef PAYLOAD_STORE_H
#define PAYLOAD_STORE_H

#include <stddef.h>

typedef enum
{
    PAYLOAD_STATUS_FETCHING,
    PAYLOAD_STATUS_READY,
    PAYLOAD_STATUS_FAILED
} payload_status_t;

void payload_store_init(void);
void payload_store_set_status(payload_status_t status);
payload_status_t payload_store_get_status(void);
void payload_store_set_data(const char *json_str);
void payload_store_get_response(char *out_buf, size_t max_len);

#endif // PAYLOAD_STORE_H