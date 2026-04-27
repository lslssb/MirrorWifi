#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"

// 日志标签
static const char *TAG = "weather_wttr";

// 天气数据结构体
typedef struct {
    char temperature[16];  // 温度 例：+22°C
    char humidity[16];     // 湿度 例：60%
} weather_data_t;

/**
 * @brief  同步获取 wttr.in 温湿度（无RTOS、不创建任务）
 * @param  weather 输出数据
 * @return 成功返回 ESP_OK
 */
esp_err_t esp_weather_get_wttr(weather_data_t *weather)
{
    if (weather == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 清空数据
    memset(weather, 0, sizeof(weather_data_t));

    // 1. 创建 TLS 句柄
    esp_tls_t *tls = esp_tls_init();
    if (tls == NULL) {
        ESP_LOGE(TAG, "TLS init failed");
        return ESP_FAIL;
    }

    // 2. TLS 配置：使用 ESP-IDF 内置证书（无需外置证书）
    esp_tls_cfg_t tls_cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    // 3. 同步建立 HTTPS 连接
    const char *url = "https://wttr.in/Beijing?format=%t+%h";
    ESP_LOGI(TAG, "Requesting: %s", url);
    int ret = esp_tls_conn_http_new_sync(url, &tls_cfg, tls);

    if (ret != 1) {
        ESP_LOGE(TAG, "HTTPS connect failed");
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    // 4. 读取服务器返回数据
    char buf[512] = {0};
    int len = esp_tls_conn_read(tls, buf, sizeof(buf) - 1);

    if (len <= 0) {
        ESP_LOGE(TAG, "Read data failed");
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "wttr.in return: %s", buf);

    // 5. 解析温度 + 湿度
    sscanf(buf, "%s %s", weather->temperature, weather->humidity);

    // 6. 释放 TLS 资源
    esp_tls_conn_destroy(tls);

    return ESP_OK;
}

/**
 * @brief  简化调用接口：直接获取并打印天气
 */
void esp_weather_wttr_run(void)
{
    ESP_LOGI(TAG, "esp_weather_wttr_run");

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