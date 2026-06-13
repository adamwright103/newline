#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "../../secret.h" // Expected to define WIFI_SSID and WIFI_PASS

#define LOCAL_URL LOCAL_PROXY_URL
#define MAX_HTTP_BUF 1024
#define SERVER_PORT 80

// Fetcher Timing & Retries
#define MAX_FETCH_RETRIES 3
#define FETCH_INTERVAL_MS (6 * 60 * 60 * 1000)  // 6 Hours
#define FETCH_INTERVAL_MS_DEBUG (1 * 60 * 1000) // 1 minute
#define FETCH_RETRY_DELAY_MS (5 * 1000)         // 5 Seconds between retries

#endif // APP_CONFIG_H