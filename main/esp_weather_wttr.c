#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

// 日志标签
static const char *TAG = "weather_wttr";

// 天气数据结构体
typedef struct {
    char temperature[16];  // 温度 例：+22°C
    char humidity[16];     // 湿度 例：60%
} weather_data_t;

// RTOS 任务句柄
static TaskHandle_t s_weather_task = NULL;

// 天气数据队列（用于传递最新天气数据）
static QueueHandle_t s_weather_queue = NULL;

// 天气事件组
static EventGroupHandle_t s_weather_event_group = NULL;
#define WEATHER_UPDATE_BIT BIT0
#define WEATHER_ERROR_BIT BIT1

// 天气更新间隔（默认 5 分钟）
#define WEATHER_UPDATE_INTERVAL_MS 300000

// 重试延迟
#define RETRY_DELAY_MS 5000

/**
 * @brief  内部函数：获取天气数据（同步）
 * @param  weather 输出数据
 * @return 成功返回 ESP_OK
 */
static esp_err_t weather_fetch_data(weather_data_t *weather)
{
    if (weather == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(weather, 0, sizeof(weather_data_t));

    esp_tls_t *tls = esp_tls_init();
    if (!tls) {
        ESP_LOGE(TAG, "TLS init failed");
        return ESP_FAIL;
    }

    esp_tls_cfg_t tls_cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    const char *url = "https://wttr.in/Beijing?format=%t+%h";
    const char *headers = "User-Agent: curl/7.68.0\r\n";

    ESP_LOGI(TAG, "Requesting: %s", url);
    int ret = esp_tls_conn_http_new_sync(url, &tls_cfg, tls);

    if (ret == 1) {
        const char *http_request = "GET /Beijing?format=%t+%h HTTP/1.1\r\n";
        const char *host_header = "Host: wttr.in\r\n";
        const char *conn_header = "Connection: close\r\n";
        const char *end_header = "\r\n";

        esp_tls_conn_write(tls, http_request, strlen(http_request));
        esp_tls_conn_write(tls, host_header, strlen(host_header));
        esp_tls_conn_write(tls, headers, strlen(headers));
        esp_tls_conn_write(tls, conn_header, strlen(conn_header));
        esp_tls_conn_write(tls, end_header, strlen(end_header));

        ESP_LOGI(TAG, "HTTP request sent");
    }

    if (ret != 1) {
        ESP_LOGE(TAG, "HTTPS connect failed");
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    char buf[1024] = {0};
    int total_len = 0;
    int len;

    while ((len = esp_tls_conn_read(tls, buf + total_len, sizeof(buf) - 1 - total_len)) > 0) {
        total_len += len;
        // 定期让出CPU，防止看门狗超时
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (total_len <= 0) {
        ESP_LOGE(TAG, "Read data failed");
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    buf[total_len] = 0;
    ESP_LOGI(TAG, "wttr.in return:\n%s", buf);

    char *body = strstr(buf, "\r\n\r\n");
    if (body) {
        body += 4;
    } else {
        body = buf;
    }

    sscanf(body, "%s %s", weather->temperature, weather->humidity);

    esp_tls_conn_destroy(tls);

    ESP_LOGI(TAG, "------------------------");
    ESP_LOGI(TAG, "温度: %s", weather->temperature);
    ESP_LOGI(TAG, "湿度: %s", weather->humidity);
    ESP_LOGI(TAG, "------------------------");

    return ESP_OK;
}

/**
 * @brief  天气任务主循环
 */
static void weather_task(void *arg)
{
    ESP_LOGI(TAG, "Weather task started");
    
    weather_data_t weather;
    
    while (1) {
        ESP_LOGI(TAG, "Fetching weather data...");
        
        esp_err_t ret = weather_fetch_data(&weather);
        
        if (ret == ESP_OK) {
            // 发送数据到队列（覆盖旧数据）
            if (s_weather_queue != NULL) {
                // 先清空队列中的旧数据
                while (uxQueueMessagesWaiting(s_weather_queue) > 0) {
                    xQueueReceive(s_weather_queue, &weather, 0);
                }
                // 发送新数据
                xQueueSend(s_weather_queue, &weather, portMAX_DELAY);
            }
            
            // 设置更新成功事件位
            xEventGroupClearBits(s_weather_event_group, WEATHER_ERROR_BIT);
            xEventGroupSetBits(s_weather_event_group, WEATHER_UPDATE_BIT);
            
            // 等待下一次更新
            vTaskDelay(pdMS_TO_TICKS(WEATHER_UPDATE_INTERVAL_MS));
        } else {
            // 设置错误事件位
            xEventGroupClearBits(s_weather_event_group, WEATHER_UPDATE_BIT);
            xEventGroupSetBits(s_weather_event_group, WEATHER_ERROR_BIT);
            
            // 失败后重试
            ESP_LOGI(TAG, "Retry after %d ms", RETRY_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
        }
    }
}

/**
 * @brief  获取最新天气数据（非阻塞）
 * @param  weather 输出数据
 * @return 成功返回 ESP_OK，队列为空返回 ESP_ERR_NOT_FOUND
 */
esp_err_t esp_weather_get_wttr(weather_data_t *weather)
{
    if (weather == NULL || s_weather_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xQueueReceive(s_weather_queue, weather, 0) == pdTRUE) {
        return ESP_OK;
    }
    
    return ESP_ERR_NOT_FOUND;
}

/**
 * @brief  初始化天气任务
 * @return 成功返回 ESP_OK
 */
esp_err_t esp_weather_wttr_init(void)
{
    ESP_LOGI(TAG, "Initializing weather task...");
    
    // 创建事件组
    s_weather_event_group = xEventGroupCreate();
    if (s_weather_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    
    // 创建队列（保存一条最新数据）
    s_weather_queue = xQueueCreate(1, sizeof(weather_data_t));
    if (s_weather_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        vEventGroupDelete(s_weather_event_group);
        return ESP_FAIL;
    }
    
    // 创建天气任务
    if (xTaskCreate(weather_task, "weather_task", 8192, NULL, 4, &s_weather_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create weather task");
        vQueueDelete(s_weather_queue);
        vEventGroupDelete(s_weather_event_group);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Weather task initialized successfully");
    return ESP_OK;
}

/**
 * @brief  获取天气事件组句柄
 * @return 事件组句柄
 */
EventGroupHandle_t esp_weather_get_event_group(void)
{
    return s_weather_event_group;
}

/**
 * @brief  获取天气数据队列句柄
 * @return 队列句柄
 */
QueueHandle_t esp_weather_get_queue(void)
{
    return s_weather_queue;
}

/**
 * @brief  简化调用接口：获取并打印天气（兼容旧接口）
 */
void esp_weather_wttr_run(void)
{
    ESP_LOGI(TAG, "esp_weather_wttr_run (compatibility mode)");
    
    // 如果任务还没初始化，先初始化
    if (s_weather_task == NULL) {
        esp_weather_wttr_init();
    }
    
    // 等待第一次更新
    if (s_weather_event_group != NULL) {
        xEventGroupWaitBits(s_weather_event_group, 
                            WEATHER_UPDATE_BIT | WEATHER_ERROR_BIT,
                            pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    }
    
    // 获取并打印天气
    weather_data_t data;
    if (esp_weather_get_wttr(&data) == ESP_OK) {
        ESP_LOGI(TAG, "------------------------");
        ESP_LOGI(TAG, "温度: %s", data.temperature);
        ESP_LOGI(TAG, "湿度: %s", data.humidity);
        ESP_LOGI(TAG, "------------------------");
    } else {
        ESP_LOGE(TAG, "Get weather failed");
    }
}