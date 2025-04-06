idf_component_register(SRCS "app_main.c" "main.c" "app_driver.c" "arduino_cloud.c"
                    INCLUDE_DIRS "."
                    REQUIRES mbedtls esp_http_client esp_https_ota json)