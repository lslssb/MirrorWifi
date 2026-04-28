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

    memset(weather, 0, sizeof(weather_data_t));

    esp_tls_t *tls = esp_tls_init();
    if (!tls) {
        ESP_LOGE(TAG, "TLS init failed");
        return ESP_FAIL;
    }

    esp_tls_cfg_t tls_cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    // ====================== 修复 1：% 必须写成 %% ======================
    const char *url = "https://wttr.in/Beijing?format=%t+%h";

    const char *headers = 
        "User-Agent: curl/7.68.0\r\n";  // wttr.in 最兼容的 UA

    ESP_LOGI(TAG, "Requesting: %s", url);
    int ret = esp_tls_conn_http_new_sync(url, &tls_cfg, tls);

    if (ret == 1) {
        // ====================== 修复 2：GET 里也必须 %% ======================
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

    // ====================== 修复 3：循环读取所有数据 ======================
    char buf[1024] = {0};
    int total_len = 0;
    int len;

    while ((len = esp_tls_conn_read(tls, buf + total_len, sizeof(buf) - 1 - total_len)) > 0) {
        total_len += len;
    }

    if (total_len <= 0) {
        ESP_LOGE(TAG, "Read data failed");
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    buf[total_len] = 0;
    ESP_LOGI(TAG, "wttr.in return:\n%s", buf);

    // ====================== 修复 4：跳过 HTTP 头，找到真正的天气数据 ======================
    char *body = strstr(buf, "\r\n\r\n");
    if (body) {
        body += 4;  // 跳过空行
    } else {
        body = buf;
    }

    // 解析温度 + 湿度
    sscanf(body, "%s %s", weather->temperature, weather->humidity);

    esp_tls_conn_destroy(tls);

    ESP_LOGI(TAG, "------------------------");
    ESP_LOGI(TAG, "温度: %s", weather->temperature);
    ESP_LOGI(TAG, "湿度: %s", weather->humidity);
    ESP_LOGI(TAG, "------------------------");

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