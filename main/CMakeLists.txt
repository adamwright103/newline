idf_component_register(SRCS "main.c"
                            "src/wifi_manager.c"
                            "src/fetcher.c"
                            "src/payload_store.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi nvs_flash esp_http_client esp_http_server cjson)