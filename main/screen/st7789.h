#ifndef ST7789_H
#define ST7789_H

#include "driver/gpio.h"
#include "driver/spi_master.h"

// ==================== 引脚定义 ====================
#define ST7789_SPI_HOST SPI2_HOST

#define ST7789_PWR_PIN GPIO_NUM_4  // 电源控制
#define ST7789_RST_PIN GPIO_NUM_16 // 复位
#define ST7789_DC_PIN GPIO_NUM_5   // 数据/命令选择
#define ST7789_CS_PIN GPIO_NUM_6   // 片选
#define ST7789_SDA_PIN GPIO_NUM_15 // MOSI
#define ST7789_SCL_PIN GPIO_NUM_7  // SCK

// ==================== 显示配置 ====================
#define TFT_COLUMN_NUMBER 240
#define TFT_LINE_NUMBER 320
#define USE_HORIZONTAL 1 // 0/1=横屏，2/3=竖屏

// ==================== 颜色定义 (RGB565) ====================
#define WHITE 0xFFFF
#define BLACK 0x0000
#define BLUE 0x001F
#define RED 0xF800
#define GREEN 0x07E0
#define BRED 0xF81F
#define GRED 0xFFE0
#define GBLUE 0x07FF
#define YELLOW 0xFFE0
#define CYAN 0x07FF
#define MAGENTA 0xF81F

static const char *TAG_ST7789 = "ST7789";

// ==================== 函数声明 ====================
void st7789_init(spi_device_handle_t *spi);
void st7789_clear(spi_device_handle_t *spi);
void st7789_fill_screen(spi_device_handle_t *spi, uint16_t color);
void st7789_draw_pixel(spi_device_handle_t *spi, uint16_t x, uint16_t y, uint16_t color);
void st7789_fill_rect(spi_device_handle_t *spi, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void st7789_delay_ms(uint32_t ms);

#endif