#include "st7735s.h"
#include "esp_err.h"
#include "assert.h"
#include "stdlib.h"
#include "string.h"
#include "esp_system.h"
#include "esp_log.h"

static unsigned char tft_column_offset = 2;
static unsigned char tft_line_offset = 3;

// ==================== 延时函数 ====================
void st7735s_delay_ms(uint32_t ms) {
    esp_rom_delay_us(ms * 1000);  // 使用 ESP32 底层延时函数
}

// ==================== SPI发送命令 ====================
void st7735s_send_cmd(spi_device_handle_t spi, uint8_t cmd) {
    esp_err_t ret;
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    
    gpio_set_level(ST7735S_SPI4W_PIN, 0);
    ret = spi_device_transmit(spi, &t);
    assert(ret == ESP_OK);
}

// ==================== SPI发送数据 ====================
void st7735s_send_data(spi_device_handle_t spi, uint8_t data) {
    esp_err_t ret;
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &data,
    };
    
    gpio_set_level(ST7735S_SPI4W_PIN, 1);
    ret = spi_device_transmit(spi, &t);
    assert(ret == ESP_OK);
}

// ==================== 设置地址窗口 ====================
static void st7735s_set_address_window(spi_device_handle_t spi, 
                                        uint16_t x0, uint16_t y0, 
                                        uint16_t x1, uint16_t y1) {
    st7735s_send_cmd(spi, 0x2A);
    st7735s_send_data(spi, (x0 >> 8) & 0xFF);
    st7735s_send_data(spi, x0 & 0xFF);
    st7735s_send_data(spi, (x1 >> 8) & 0xFF);
    st7735s_send_data(spi, x1 & 0xFF);

    st7735s_send_cmd(spi, 0x2B);
    st7735s_send_data(spi, (y0 >> 8) & 0xFF);
    st7735s_send_data(spi, y0 & 0xFF);
    st7735s_send_data(spi, (y1 >> 8) & 0xFF);
    st7735s_send_data(spi, y1 & 0xFF);

    st7735s_send_cmd(spi, 0x2C);
}

// ==================== 清屏 ====================
void st7735s_clear(spi_device_handle_t spi) {
    st7735s_fill_screen(spi, BLACK);
}

// ==================== 全屏填充颜色 ====================
void st7735s_fill_screen(spi_device_handle_t spi, uint16_t color) {
    uint32_t row;
    
    st7735s_set_address_window(spi, 
                               tft_column_offset, 
                               tft_line_offset, 
                               TFT_COLUMN_NUMBER - 1 + tft_column_offset, 
                               TFT_LINE_NUMBER - 1 + tft_line_offset);
    
    spi_transaction_t t = {
        .length = TFT_COLUMN_NUMBER * 2 * 8,
    };
    
    gpio_set_level(ST7735S_SPI4W_PIN, 1);
    
    uint8_t *buffer = (uint8_t *)malloc(TFT_COLUMN_NUMBER * 2);
    if (buffer == NULL) return;
    
    for (uint32_t col = 0; col < TFT_COLUMN_NUMBER; col++) {
        buffer[col * 2] = (color >> 8) & 0xFF;
        buffer[col * 2 + 1] = color & 0xFF;
    }
    t.tx_buffer = buffer;
    
    for (row = 0; row < TFT_LINE_NUMBER; row++) {
        spi_device_transmit(spi, &t);
    }
    
    free(buffer);
}

// ==================== ST7735S初始化 ====================
void st7735s_init(spi_device_handle_t spi) {
    ESP_LOGI("ST7735S", "Initializing ST7735S display...");
    
    // ========== 电源引脚初始化（给屏幕供电）==========
    ESP_LOGI("ST7735S", "Configuring power pin (GPIO%d)", ST7735S_PWR_PIN);
    gpio_config_t pwr_conf = {
        .pin_bit_mask = (1ULL << ST7735S_PWR_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwr_conf);
    
    // 开启屏幕电源
    ESP_LOGI("ST7735S", "Turning on display power...");
    gpio_set_level(ST7735S_PWR_PIN, 1);
    esp_rom_delay_us(50000);  // 延长到50ms确保电源稳定
    
    // 复位引脚
    gpio_config_t rst_conf = {
        .pin_bit_mask = (1ULL << ST7735S_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_conf);
    
    // DC引脚
    gpio_config_t dc_conf = {
        .pin_bit_mask = (1ULL << ST7735S_SPI4W_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&dc_conf);
    
    // 硬件复位
    gpio_set_level(ST7735S_RST_PIN, 0);
    esp_rom_delay_us(100000);
    gpio_set_level(ST7735S_RST_PIN, 1);
    esp_rom_delay_us(100000);

    // 退出睡眠
    st7735s_send_cmd(spi, 0x11);
    esp_rom_delay_us(120000);

    // 帧速率控制
    st7735s_send_cmd(spi, 0xB1);
    st7735s_send_data(spi, 0x05);
    st7735s_send_data(spi, 0x3A);
    st7735s_send_data(spi, 0x3A);

    st7735s_send_cmd(spi, 0xB2);
    st7735s_send_data(spi, 0x05);
    st7735s_send_data(spi, 0x3A);
    st7735s_send_data(spi, 0x3A);

    st7735s_send_cmd(spi, 0xB3);
    st7735s_send_data(spi, 0x05);
    st7735s_send_data(spi, 0x3A);
    st7735s_send_data(spi, 0x3A);
    st7735s_send_data(spi, 0x05);
    st7735s_send_data(spi, 0x3A);
    st7735s_send_data(spi, 0x3A);

    st7735s_send_cmd(spi, 0xB4);
    st7735s_send_data(spi, 0x03);

    // 电源控制
    st7735s_send_cmd(spi, 0xC0);
    st7735s_send_data(spi, 0X28);
    st7735s_send_data(spi, 0X08);
    st7735s_send_data(spi, 0X04);

    st7735s_send_cmd(spi, 0xC1);
    st7735s_send_data(spi, 0XC0);

    st7735s_send_cmd(spi, 0xC2);
    st7735s_send_data(spi, 0X0D);
    st7735s_send_data(spi, 0X00);

    st7735s_send_cmd(spi, 0xC3);
    st7735s_send_data(spi, 0X8D);
    st7735s_send_data(spi, 0X2A);

    st7735s_send_cmd(spi, 0xC4);
    st7735s_send_data(spi, 0X8D);
    st7735s_send_data(spi, 0XEE);

    st7735s_send_cmd(spi, 0xC5);
    st7735s_send_data(spi, 0X1F);

    // 显示方向
    st7735s_send_cmd(spi, 0x36);
    st7735s_send_data(spi, 0xC8);

    // Gamma校正
    st7735s_send_cmd(spi, 0XE0);
    st7735s_send_data(spi, 0x04); st7735s_send_data(spi, 0x22);
    st7735s_send_data(spi, 0x07); st7735s_send_data(spi, 0x0A);
    st7735s_send_data(spi, 0x2E); st7735s_send_data(spi, 0x30);
    st7735s_send_data(spi, 0x25); st7735s_send_data(spi, 0x2A);
    st7735s_send_data(spi, 0x28); st7735s_send_data(spi, 0x26);
    st7735s_send_data(spi, 0x2E); st7735s_send_data(spi, 0x3A);
    st7735s_send_data(spi, 0x00); st7735s_send_data(spi, 0x01);
    st7735s_send_data(spi, 0x03); st7735s_send_data(spi, 0x13);

    st7735s_send_cmd(spi, 0XE1);
    st7735s_send_data(spi, 0x04); st7735s_send_data(spi, 0x16);
    st7735s_send_data(spi, 0x06); st7735s_send_data(spi, 0x0D);
    st7735s_send_data(spi, 0x2D); st7735s_send_data(spi, 0x26);
    st7735s_send_data(spi, 0x23); st7735s_send_data(spi, 0x27);
    st7735s_send_data(spi, 0x27); st7735s_send_data(spi, 0x25);
    st7735s_send_data(spi, 0x2D); st7735s_send_data(spi, 0x3B);
    st7735s_send_data(spi, 0x00); st7735s_send_data(spi, 0x01);
    st7735s_send_data(spi, 0x04); st7735s_send_data(spi, 0x13);

    // 像素格式
    st7735s_send_cmd(spi, 0x3A);
    st7735s_send_data(spi, 0x05);

    // 显示开关
    st7735s_send_cmd(spi, 0x29);
}