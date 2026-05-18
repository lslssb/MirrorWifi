#pragma once

#include "freertos/queue.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

// 天气数据结构
typedef struct {
    char temperature[16];
    char humidity[16];
} weather_data_t;

// 天气事件位定义
#define WEATHER_UPDATE_BIT BIT0
#define WEATHER_ERROR_BIT BIT1

// ==================== RTOS 接口 ====================

/**
 * @brief  初始化天气任务（创建独立任务定期获取天气）
 * @return 成功返回 ESP_OK
 */
esp_err_t esp_weather_wttr_init(void);

/**
 * @brief  获取最新天气数据（非阻塞）
 * @param  weather 输出数据
 * @return 成功返回 ESP_OK，队列为空返回 ESP_ERR_NOT_FOUND
 */
esp_err_t esp_weather_get_wttr(weather_data_t *weather);

/**
 * @brief  获取天气事件组句柄
 * @return 事件组句柄
 */
EventGroupHandle_t esp_weather_get_event_group(void);

/**
 * @brief  获取天气数据队列句柄
 * @return 队列句柄
 */
QueueHandle_t esp_weather_get_queue(void);

// ==================== 兼容接口 ====================

/**
 * @brief  简化调用接口：获取并打印天气（兼容旧接口）
 */
void esp_weather_wttr_run(void);

#ifdef __cplusplus
}
#endif