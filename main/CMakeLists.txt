idf_component_register(SRCS "main.c"
                            "src/wifi_manager.c"
                            "src/fetcher.c"
                            "src/payload_store.c"
                            "src/display.c"
                            "assets/fonts/azeret41/azeret_41.c"
                            "assets/fonts/sono_21/sono_21.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi nvs_flash esp_http_client esp_http_server cjson esp_driver_spi esp_driver_gpio driver)

target_add_binary_data(${COMPONENT_LIB} "assets/static_ui.bin" BINARY)