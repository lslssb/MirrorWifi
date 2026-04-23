#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 天气数据结构
typedef struct {
    char temperature[16];
    char humidity[16];
} weather_data_t;

// 运行一次获取天气（无RTOS）
void esp_weather_wttr_run(void);

// 底层获取接口
esp_err_t esp_weather_get_wttr(weather_data_t *weather);

#ifdef __cplusplus
}
#endif