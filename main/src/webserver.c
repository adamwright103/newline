#include "webserver.h"
#include "payload_store.h"
#include "app_config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "WEBSERVER";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    char *resp_buf = malloc(MAX_HTTP_BUF);
    if (resp_buf == NULL)
    {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    payload_store_get_response(resp_buf, MAX_HTTP_BUF);
    httpd_resp_send(req, resp_buf, HTTPD_RESP_USE_STRLEN);

    free(resp_buf);
    return ESP_OK;
}

void webserver_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = SERVER_PORT;

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