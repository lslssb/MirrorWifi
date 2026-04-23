idf_component_register(SRCS "softap_sta.c" "esp_weather_wttr.c"
                    PRIV_REQUIRES esp_wifi nvs_flash esp_http_server esp-tls cjson
                    INCLUDE_DIRS ".")