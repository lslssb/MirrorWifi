/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/*  WiFi softAP & station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_net_stack.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#if IP_NAPT
#include "lwip/lwip_napt.h"
#endif
#include "lwip/err.h"
#include "lwip/sys.h"

/* The examples use WiFi configuration that you can set via project configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_ESP_WIFI_STA_SSID "mywifissid"
*/

/* STA Configuration */
#define EXAMPLE_ESP_WIFI_STA_SSID           CONFIG_ESP_WIFI_REMOTE_AP_SSID
#define EXAMPLE_ESP_WIFI_STA_PASSWD         CONFIG_ESP_WIFI_REMOTE_AP_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY           CONFIG_ESP_MAXIMUM_STA_RETRY

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WAPI_PSK
#endif

/* AP Configuration */
#define EXAMPLE_ESP_WIFI_AP_SSID            CONFIG_ESP_WIFI_AP_SSID
#define EXAMPLE_ESP_WIFI_AP_PASSWD          CONFIG_ESP_WIFI_AP_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL            CONFIG_ESP_WIFI_AP_CHANNEL
#define EXAMPLE_MAX_STA_CONN                CONFIG_ESP_MAX_STA_CONN_AP


/* DHCP server option */
#define DHCPS_OFFER_DNS             0x02

static const char *TAG_AP = "WiFi SoftAP";
static const char *TAG_STA = "WiFi Sta";
static const char *TAG_HTTP = "HTTP Server";

/* Connection status flag for bare-metal implementation */
static bool s_sta_connected = false;
static httpd_handle_t s_http_server = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG_AP, "Station "MACSTR" joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG_AP, "Station "MACSTR" left, AID=%d, reason:%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG_STA, "Station started");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG_STA, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_sta_connected = true;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_ASSIGNED_IP_TO_CLIENT) {
        const ip_event_assigned_ip_to_client_t *e = (const ip_event_assigned_ip_to_client_t *)event_data;
        ESP_LOGI(TAG_AP, "Assigned IP to client: " IPSTR ", MAC=" MACSTR ", hostname='%s'",
                 IP2STR(&e->ip), MAC2STR(e->mac), e->hostname);
    }
}

/* HTML 配置页面 */
static const char *index_html = "<!DOCTYPE html>"
"<html>"
"<head>"
"    <meta charset=\"UTF-8\">"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
"    <title>WiFi 配置</title>"
"    <style>"
"        body {"
"            font-family: Arial, sans-serif;"
"            margin: 20px;"
"            padding: 0;"
"            background-color: #f0f0f0;"
"        }"
"        h1 {"
"            color: #333;"
"            text-align: center;"
"        }"
"        form {"
"            background-color: white;"
"            padding: 20px;"
"            border-radius: 8px;"
"            box-shadow: 0 0 10px rgba(0,0,0,0.1);"
"            max-width: 400px;"
"            margin: 0 auto;"
"        }"
"        label {"
"            display: block;"
"            margin-bottom: 8px;"
"            font-weight: bold;"
"        }"
"        input[type=\"text\"], input[type=\"password\"] {"
"            width: 100%;"
"            padding: 10px;"
"            margin-bottom: 15px;"
"            border: 1px solid #ddd;"
"            border-radius: 4px;"
"            box-sizing: border-box;"
"        }"
"        input[type=\"submit\"] {"
"            background-color: #4CAF50;"
"            color: white;"
"            padding: 10px 15px;"
"            border: none;"
"            border-radius: 4px;"
"            cursor: pointer;"
"            width: 100%;"
"            font-size: 16px;"
"        }"
"        input[type=\"submit\"]:hover {"
"            background-color: #45a049;"
"        }"
"        .status {"
"            margin-top: 20px;"
"            padding: 10px;"
"            border-radius: 4px;"
"            text-align: center;"
"        }"
"        .success {"
"            background-color: #d4edda;"
"            color: #155724;"
"            border: 1px solid #c3e6cb;"
"        }"
"        .error {"
"            background-color: #f8d7da;"
"            color: #721c24;"
"            border: 1px solid #f5c6cb;"
"        }"
"        select {"
"            width: 100%;"
"            padding: 10px;"
"            margin-bottom: 15px;"
"            border: 1px solid #ddd;"
"            border-radius: 4px;"
"            box-sizing: border-box;"
"        }"
"    </style>"
"</head>"
"<body>"
"    <h1>WiFi 配置</h1>"
"    <form action=\"/config\" method=\"post\">"
"        <label for=\"ssid\">WiFi 名称 (SSID):</label>"
"        <select name=\"ssid\" id=\"ssid\">"
"            %s"
"        </select>"
"        <label for=\"password\">WiFi 密码:</label>"
"        <input type=\"password\" id=\"password\" name=\"password\" placeholder=\"请输入密码\">"
"        <input type=\"submit\" value=\"保存配置\">"
"    </form>"
"    %s"
"</body>"
"</html>";


/* 扫描 WiFi 网络 */
static char *scan_wifi_networks(void)
{
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true
    };
    
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "WiFi scan failed: %s", esp_err_to_name(ret));
        return strdup("<option value=\"\">扫描失败</option>");
    }
    
    uint16_t ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "Get AP count failed: %s", esp_err_to_name(ret));
        return strdup("<option value=\"\">获取 AP 数量失败</option>");
    }
    
    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_records) {
        ESP_LOGE(TAG_HTTP, "Malloc failed");
        return strdup("<option value=\"\">内存分配失败</option>");
    }
    
    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "Get AP records failed: %s", esp_err_to_name(ret));
        free(ap_records);
        return strdup("<option value=\"\">获取 AP 记录失败</option>");
    }
    
    char *options = malloc(1024);
    if (!options) {
        ESP_LOGE(TAG_HTTP, "Malloc failed");
        free(ap_records);
        return strdup("<option value=\"\">内存分配失败</option>");
    }
    
    options[0] = '\0';
    for (int i = 0; i < ap_count; i++) {
        char option[256];
        snprintf(option, sizeof(option), "<option value=\"%s\">%s (信号强度: %d dBm)</option>\n", 
                 (char *)ap_records[i].ssid, (char *)ap_records[i].ssid, ap_records[i].rssi);
        strcat(options, option);
    }
    
    free(ap_records);
    return options;
}

/* 处理根路径请求 */
static esp_err_t index_handler(httpd_req_t *req)
{
    char *wifi_options = scan_wifi_networks();
    char *status_msg = "";
    
    char *response = malloc(strlen(index_html) + strlen(wifi_options) + strlen(status_msg) + 100);
    if (!response) {
        httpd_resp_send_500(req);
        free(wifi_options);
        return ESP_FAIL;
    }
    
    sprintf(response, index_html, wifi_options, status_msg);
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    
    free(response);
    free(wifi_options);
    return ESP_OK;
}

/* 处理配置提交 */
static esp_err_t config_handler(httpd_req_t *req)
{
    char buf[512];
    int ret, remaining = req->content_len;
    
    while (remaining > 0) {
        ret = httpd_req_recv(req, buf, (remaining < sizeof(buf)) ? remaining : sizeof(buf));
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            return ESP_FAIL;
        }
        buf[ret] = '\0';
        remaining -= ret;
    }
    
    // 解析表单数据
    char ssid[32] = {0};
    char password[64] = {0};
    
    char *ssid_ptr = strstr(buf, "ssid=");
    char *password_ptr = strstr(buf, "password=");
    
    if (ssid_ptr && password_ptr) {
        // 提取 SSID
        ssid_ptr += 5; // 跳过 "ssid="
        char *ssid_end = strstr(ssid_ptr, "&");
        if (ssid_end) {
            strncpy(ssid, ssid_ptr, ssid_end - ssid_ptr);
        } else {
            strcpy(ssid, ssid_ptr);
        }
        
        // 提取密码
        password_ptr += 9; // 跳过 "password="
        strcpy(password, password_ptr);
        
        // 解码 URL 编码的字符
        for (int i = 0; i < strlen(ssid); i++) {
            if (ssid[i] == '+') ssid[i] = ' ';
        }
        for (int i = 0; i < strlen(password); i++) {
            if (password[i] == '+') password[i] = ' ';
        }
        
        ESP_LOGI(TAG_HTTP, "Received SSID: %s, Password: %s", ssid, password);
        
        // 保存配置到 NVS
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
        if (err == ESP_OK) {
            err = nvs_set_str(nvs_handle, "ssid", ssid);
            if (err == ESP_OK) {
                err = nvs_set_str(nvs_handle, "password", password);
                if (err == ESP_OK) {
                    err = nvs_commit(nvs_handle);
                }
            }
            nvs_close(nvs_handle);
        }
        
        if (err == ESP_OK) {
            ESP_LOGI(TAG_HTTP, "WiFi config saved to NVS");
            
            // 重启 WiFi 连接
            wifi_config_t wifi_sta_config = {
                .sta = {
                    .ssid = "",
                    .password = "",
                },
            };
            strcpy((char *)wifi_sta_config.sta.ssid, ssid);
            strcpy((char *)wifi_sta_config.sta.password, password);
            
            esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config);
            esp_wifi_disconnect();
            esp_wifi_connect();
            
            // 发送成功响应
            char *wifi_options = scan_wifi_networks();
            char *status_msg = "<div class=\"status success\">配置已保存，正在连接...</div>";
            
            char *response = malloc(strlen(index_html) + strlen(wifi_options) + strlen(status_msg) + 100);
            if (response) {
                sprintf(response, index_html, wifi_options, status_msg);
                httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
                free(response);
            } else {
                httpd_resp_send_500(req);
            }
            free(wifi_options);
        } else {
            ESP_LOGE(TAG_HTTP, "Failed to save WiFi config: %s", esp_err_to_name(err));
            
            // 发送错误响应
            char *wifi_options = scan_wifi_networks();
            char *status_msg = "<div class=\"status error\">保存配置失败</div>";
            
            char *response = malloc(strlen(index_html) + strlen(wifi_options) + strlen(status_msg) + 100);
            if (response) {
                sprintf(response, index_html, wifi_options, status_msg);
                httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
                free(response);
            } else {
                httpd_resp_send_500(req);
            }
            free(wifi_options);
        }
    } else {
        httpd_resp_send_500(req);
    }
    
    return ESP_OK;
}

/* 启动 HTTP 服务器 */
static httpd_handle_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    
    ESP_LOGI(TAG_HTTP, "Starting HTTP server on port %d", config.server_port);
    if (httpd_start(&s_http_server, &config) == ESP_OK) {
        // 注册 URI 处理程序
        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_http_server, &index_uri);
        
        httpd_uri_t config_uri = {
            .uri = "/config",
            .method = HTTP_POST,
            .handler = config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_http_server, &config_uri);
        
        return s_http_server;
    }
    
    ESP_LOGE(TAG_HTTP, "Failed to start HTTP server");
    return NULL;
}



/* Initialize soft AP */
esp_netif_t *wifi_init_softap(void)
{
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_AP_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_AP_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_AP_PASSWD,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    if (strlen(EXAMPLE_ESP_WIFI_AP_PASSWD) == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    ESP_LOGI(TAG_AP, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_AP_SSID, EXAMPLE_ESP_WIFI_AP_PASSWD, EXAMPLE_ESP_WIFI_CHANNEL);

    return esp_netif_ap;
}

/* Initialize wifi station */
esp_netif_t *wifi_init_sta(void)
{
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_sta_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_STA_SSID,
            .password = EXAMPLE_ESP_WIFI_STA_PASSWD,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .failure_retry_cnt = EXAMPLE_ESP_MAXIMUM_RETRY,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
            * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    // 尝试从 NVS 加载配置
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        char ssid[32] = {0};
        char password[64] = {0};
        size_t ssid_len = sizeof(ssid);
        size_t password_len = sizeof(password);
        
        err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
        if (err == ESP_OK) {
            err = nvs_get_str(nvs_handle, "password", password, &password_len);
            if (err == ESP_OK) {
                ESP_LOGI(TAG_STA, "Loaded WiFi config from NVS: SSID=%s", ssid);
                strcpy((char *)wifi_sta_config.sta.ssid, ssid);
                strcpy((char *)wifi_sta_config.sta.password, password);
            }
        }
        nvs_close(nvs_handle);
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config) );

    ESP_LOGI(TAG_STA, "wifi_init_sta finished.");

    return esp_netif_sta;
}

void softap_set_dns_addr(esp_netif_t *esp_netif_ap,esp_netif_t *esp_netif_sta)
{
    esp_netif_dns_info_t dns;
    esp_netif_get_dns_info(esp_netif_sta,ESP_NETIF_DNS_MAIN,&dns);
    uint8_t dhcps_offer_option = DHCPS_OFFER_DNS;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(esp_netif_ap));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(esp_netif_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dhcps_offer_option, sizeof(dhcps_offer_option)));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(esp_netif_ap, ESP_NETIF_DNS_MAIN, &dns));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(esp_netif_ap));
}

void app_main(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Register Event handler */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &wifi_event_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_ASSIGNED_IP_TO_CLIENT,
                    &wifi_event_handler,
                    NULL,
                    NULL));

    /*Initialize WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    /* Initialize AP */
    ESP_LOGI(TAG_AP, "ESP_WIFI_MODE_AP");
    esp_netif_t *esp_netif_ap = wifi_init_softap();

    /* Initialize STA */
    ESP_LOGI(TAG_STA, "ESP_WIFI_MODE_STA");
    esp_netif_t *esp_netif_sta = wifi_init_sta();

    /* Start WiFi */
    ESP_ERROR_CHECK(esp_wifi_start() );

    /* Start HTTP server for WiFi configuration */
    start_http_server();
    ESP_LOGI(TAG_HTTP, "HTTP server started, access http://192.168.4.1 to configure WiFi");

    /* Wait for station to connect (simple polling for bare-metal) */
    int retry_count = 0;
    while (!s_sta_connected && retry_count < EXAMPLE_ESP_MAXIMUM_RETRY) {
        ESP_LOGI(TAG_STA, "Waiting for station to connect... (%d/%d)", retry_count + 1, EXAMPLE_ESP_MAXIMUM_RETRY);
        // Simple delay for bare-metal
        for (int i = 0; i < 1000000; i++) {
            __asm__ volatile ("nop");
        }
        retry_count++;
    }

    if (s_sta_connected) {
        // 从 NVS 加载配置以获取实际连接的 SSID
        char ssid[32] = {0};
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
        if (err == ESP_OK) {
            size_t ssid_len = sizeof(ssid);
            err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
            nvs_close(nvs_handle);
        }
        
        if (strlen(ssid) > 0) {
            ESP_LOGI(TAG_STA, "connected to ap SSID:%s", ssid);
        } else {
            ESP_LOGI(TAG_STA, "connected to ap SSID:%s password:%s",
                     EXAMPLE_ESP_WIFI_STA_SSID, EXAMPLE_ESP_WIFI_STA_PASSWD);
        }
        
        softap_set_dns_addr(esp_netif_ap,esp_netif_sta);
    } else {
        ESP_LOGI(TAG_STA, "Failed to connect to WiFi, please configure via http://192.168.4.1");
    }

    /* Set sta as the default interface */
    esp_netif_set_default_netif(esp_netif_sta);

    /* Enable napt on the AP netif */
    if (esp_netif_napt_enable(esp_netif_ap) != ESP_OK) {
        ESP_LOGE(TAG_STA, "NAPT not enabled on the netif: %p", esp_netif_ap);
    }
}