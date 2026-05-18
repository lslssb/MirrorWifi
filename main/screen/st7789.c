#include "st7789.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ==================== 延时函数 ====================
void st7789_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// ==================== 发送命令 ====================
void st7789_send_cmd(spi_device_handle_t *spi, uint8_t cmd)
{
    esp_err_t ret;
    uint8_t tx_data[1] = {cmd};
    
    spi_transaction_t t = {
        .length = 8,                     // 8 bits command
        .tx_buffer = tx_data,
        .user = (void *)0,               // DC=0 for command
    };
    
    // DC引脚拉低表示命令
    gpio_set_level(ST7789_DC_PIN, 0);
    ret = spi_device_transmit(*spi, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_ST7789, "Failed to send command 0x%02X", cmd);
    }
}

// ==================== 发送数据字节 ====================
void st7789_send_data(spi_device_handle_t *spi, uint8_t data)
{
    esp_err_t ret;
    uint8_t tx_data[1] = {data};
    
    spi_transaction_t t = {
        .length = 8,                     // 8 bits data
        .tx_buffer = tx_data,
        .user = (void *)1,               // DC=1 for data
    };
    
    // DC引脚拉高表示数据
    gpio_set_level(ST7789_DC_PIN, 1);
    ret = spi_device_transmit(*spi, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_ST7789, "Failed to send data 0x%02X", data);
    }
}

// ==================== 发送16位颜色数据 ====================
void st7789_send_color(spi_device_handle_t *spi, uint16_t color)
{
    esp_err_t ret;
    uint8_t tx_data[2] = {
        (color >> 8) & 0xFF,  // 高字节
        color & 0xFF          // 低字节
    };
    
    spi_transaction_t t = {
        .length = 16,                    // 16 bits color
        .tx_buffer = tx_data,
    };
    
    gpio_set_level(ST7789_DC_PIN, 1);
    ret = spi_device_transmit(*spi, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_ST7789, "Failed to send color 0x%04X", color);
    }
}

// ==================== 设置显示窗口 ====================
void st7789_set_address_window(spi_device_handle_t *spi, uint16_t x, uint16_t y, uint16_t x_end, uint16_t y_end)
{
    // 设置列地址
    st7789_send_cmd(spi, 0x2A);
    st7789_send_data(spi, x >> 8);
    st7789_send_data(spi, x & 0xFF);
    st7789_send_data(spi, x_end >> 8);
    st7789_send_data(spi, x_end & 0xFF);
    
    // 设置行地址
    st7789_send_cmd(spi, 0x2B);
    st7789_send_data(spi, y >> 8);
    st7789_send_data(spi, y & 0xFF);
    st7789_send_data(spi, y_end >> 8);
    st7789_send_data(spi, y_end & 0xFF);
    
    // 写入GRAM命令
    st7789_send_cmd(spi, 0x2C);
}

// ==================== 初始化（4线SPI模式） ====================
void st7789_init(spi_device_handle_t *spi)
{
    esp_err_t ret;
    
    // 配置GPIO引脚
    gpio_config_t gpio_conf = {
        .pin_bit_mask = (1ULL << ST7789_PWR_PIN) |
                        (1ULL << ST7789_RST_PIN) |
                        (1ULL << ST7789_DC_PIN) |
                        (1ULL << ST7789_CS_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = false,
    };
    gpio_config(&gpio_conf);
    
    // 上电
    gpio_set_level(ST7789_PWR_PIN, 1);
    st7789_delay_ms(100);
    
    // 硬件复位
    gpio_set_level(ST7789_CS_PIN, 1);
    gpio_set_level(ST7789_RST_PIN, 0);
    st7789_delay_ms(100);
    gpio_set_level(ST7789_RST_PIN, 1);
    st7789_delay_ms(100);
    
    // SPI总线初始化
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = ST7789_SDA_PIN,
        .sclk_io_num = ST7789_SCL_PIN,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    
    ret = spi_bus_initialize(ST7789_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_ST7789, "SPI bus init failed: %s", esp_err_to_name(ret));
        *spi = NULL;
        return;
    }
    
    // SPI设备配置
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 40 * 1000 * 1000,  // 40MHz
        .mode = 0,                            // SPI模式0
        .spics_io_num = ST7789_CS_PIN,
        .queue_size = 16,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };
    
    ret = spi_bus_add_device(ST7789_SPI_HOST, &dev_cfg, spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_ST7789, "SPI device add failed: %s", esp_err_to_name(ret));
        *spi = NULL;
        return;
    }
    
    // ==================== ST7789 初始化序列 ====================
    ESP_LOGI(TAG_ST7789, "Starting ST7789 initialization...");
    
    // 退出睡眠模式
    st7789_send_cmd(spi, 0x11);
    st7789_delay_ms(120);
    
    // 内存访问控制（设置显示方向）
    st7789_send_cmd(spi, 0x36);
    uint8_t madctl = 0x00;
    if (USE_HORIZONTAL == 0) madctl = 0x00;
    else if (USE_HORIZONTAL == 1) madctl = 0xC0;
    else if (USE_HORIZONTAL == 2) madctl = 0x70;
    else madctl = 0xA0;
    st7789_send_data(spi, madctl);
    
    // 颜色模式：16位RGB565
    st7789_send_cmd(spi, 0x3A);
    st7789_send_data(spi, 0x05);
    
    // 帧速率控制
    st7789_send_cmd(spi, 0xB2);
    st7789_send_data(spi, 0x05);
    st7789_send_data(spi, 0x05);
    st7789_send_data(spi, 0x00);
    st7789_send_data(spi, 0x33);
    st7789_send_data(spi, 0x33);
    
    // 伽马控制
    st7789_send_cmd(spi, 0xB7);
    st7789_send_data(spi, 0x35);
    
    // VCOM设置
    st7789_send_cmd(spi, 0xBB);
    st7789_send_data(spi, 0x21);
    
    // 电源控制1
    st7789_send_cmd(spi, 0xC0);
    st7789_send_data(spi, 0x2C);
    
    // 电压控制
    st7789_send_cmd(spi, 0xC2);
    st7789_send_data(spi, 0x01);
    
    // VCOM偏移
    st7789_send_cmd(spi, 0xC3);
    st7789_send_data(spi, 0x0B);
    
    // VCOM幅度
    st7789_send_cmd(spi, 0xC4);
    st7789_send_data(spi, 0x20);
    
    // 帧速率控制2
    st7789_send_cmd(spi, 0xC6);
    st7789_send_data(spi, 0x01);  // 111Hz
    
    // 电源控制A
    st7789_send_cmd(spi, 0xD0);
    st7789_send_data(spi, 0xA4);
    st7789_send_data(spi, 0xA1);
    
    // 反显控制
    st7789_send_cmd(spi, 0xD6);
    st7789_send_data(spi, 0xA1);
    
    // 伽马校正正增益
    st7789_send_cmd(spi, 0xE0);
    st7789_send_data(spi, 0xD0);
    st7789_send_data(spi, 0x04);
    st7789_send_data(spi, 0x08);
    st7789_send_data(spi, 0x0A);
    st7789_send_data(spi, 0x09);
    st7789_send_data(spi, 0x05);
    st7789_send_data(spi, 0x2D);
    st7789_send_data(spi, 0x43);
    st7789_send_data(spi, 0x49);
    st7789_send_data(spi, 0x09);
    st7789_send_data(spi, 0x16);
    st7789_send_data(spi, 0x15);
    st7789_send_data(spi, 0x26);
    st7789_send_data(spi, 0x2B);
    
    // 伽马校正负增益
    st7789_send_cmd(spi, 0xE1);
    st7789_send_data(spi, 0xD0);
    st7789_send_data(spi, 0x03);
    st7789_send_data(spi, 0x09);
    st7789_send_data(spi, 0x0A);
    st7789_send_data(spi, 0x0A);
    st7789_send_data(spi, 0x06);
    st7789_send_data(spi, 0x2E);
    st7789_send_data(spi, 0x44);
    st7789_send_data(spi, 0x40);
    st7789_send_data(spi, 0x3A);
    st7789_send_data(spi, 0x15);
    st7789_send_data(spi, 0x15);
    st7789_send_data(spi, 0x26);
    st7789_send_data(spi, 0x2A);
    
    // 开启反显模式
    st7789_send_cmd(spi, 0x21);
    
    // 开启显示
    st7789_send_cmd(spi, 0x29);
    st7789_delay_ms(10);
    
    ESP_LOGI(TAG_ST7789, "ST7789 initialization complete");
}

// ==================== 全屏填充 ====================
void st7789_fill_screen(spi_device_handle_t *spi, uint16_t color)
{
    if (spi == NULL || *spi == NULL) return;
    
    st7789_set_address_window(spi, 0, 0, TFT_COLUMN_NUMBER - 1, TFT_LINE_NUMBER - 1);
    
    uint32_t total_pixels = (uint32_t)TFT_COLUMN_NUMBER * TFT_LINE_NUMBER;
    uint8_t h = (color >> 8) & 0xFF;
    uint8_t l = color & 0xFF;
    
    gpio_set_level(ST7789_DC_PIN, 1);
    
    // 使用DMA批量发送数据，减少阻塞时间
    // 每个批次发送1024个像素（2048字节）
    #define BATCH_SIZE 1024
    uint8_t batch_buffer[BATCH_SIZE * 2];
    
    // 填充批次缓冲区
    for (int i = 0; i < BATCH_SIZE; i++) {
        batch_buffer[i * 2] = h;
        batch_buffer[i * 2 + 1] = l;
    }
    
    uint32_t remaining = total_pixels;
    esp_err_t ret;
    
    while (remaining > 0) {
        uint32_t send_count = (remaining > BATCH_SIZE) ? BATCH_SIZE : remaining;
        
        spi_transaction_t t = {
            .length = send_count * 16,  // 每个像素16位
            .tx_buffer = batch_buffer,
        };
        
        ret = spi_device_transmit(*spi, &t);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_ST7789, "Failed to send fill screen data");
            break;
        }
        
        remaining -= send_count;
        
        // 定期让出CPU，防止看门狗超时
        if (remaining > 0) {
            taskYIELD();
        }
    }
}

// ==================== 清屏（黑色） ====================
void st7789_clear(spi_device_handle_t *spi)
{
    st7789_fill_screen(spi, BLACK);
}

// ==================== 绘制单个像素 ====================
void st7789_draw_pixel(spi_device_handle_t *spi, uint16_t x, uint16_t y, uint16_t color)
{
    if (spi == NULL || *spi == NULL) return;
    if (x >= TFT_COLUMN_NUMBER || y >= TFT_LINE_NUMBER) return;
    
    st7789_set_address_window(spi, x, y, x, y);
    st7789_send_color(spi, color);
}

// ==================== 填充矩形区域 ====================
void st7789_fill_rect(spi_device_handle_t *spi, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (spi == NULL || *spi == NULL) return;
    if (x >= TFT_COLUMN_NUMBER || y >= TFT_LINE_NUMBER) return;
    
    // 计算有效区域
    uint16_t x_end = x + w - 1;
    uint16_t y_end = y + h - 1;
    
    if (x_end >= TFT_COLUMN_NUMBER) x_end = TFT_COLUMN_NUMBER - 1;
    if (y_end >= TFT_LINE_NUMBER) y_end = TFT_LINE_NUMBER - 1;
    
    st7789_set_address_window(spi, x, y, x_end, y_end);
    
    uint8_t color_h = (color >> 8) & 0xFF;
    uint8_t color_l = color & 0xFF;
    
    gpio_set_level(ST7789_DC_PIN, 1);
    
    for (uint16_t row = y; row <= y_end; row++) {
        for (uint16_t col = x; col <= x_end; col++) {
            st7789_send_data(spi, color_h);
            st7789_send_data(spi, color_l);
        }
    }
}