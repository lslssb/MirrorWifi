idf_component_register(SRCS "softap_sta.c" "esp_weather_wttr.c" "screen/st7735s.c"
                    PRIV_REQUIRES esp_wifi nvs_flash esp_http_server esp-tls cjson driver esp_timer
                    INCLUDE_DIRS ".")