#ifndef ST7735S_H
#define ST7735S_H

#include "driver/gpio.h"
#include "driver/spi_master.h"

// ==================== 引脚定义 ====================
#define ST7735S_SPI_HOST    SPI2_HOST

#define ST7735S_PWR_PIN     GPIO_NUM_4
#define ST7735S_RST_PIN     GPIO_NUM_8
#define ST7735S_RS_PIN      GPIO_NUM_9
#define ST7735S_SPI4W_PIN   GPIO_NUM_17 // 模式脚：0=4线SPI，1=3线SPI
#define ST7735S_CS_PIN      GPIO_NUM_18
#define ST7735S_SDA_PIN     GPIO_NUM_19
#define ST7735S_SCL_PIN     GPIO_NUM_20

// ==================== 显示配置 ====================
#define TFT_COLUMN_NUMBER   128
#define TFT_LINE_NUMBER     128
#define USE_LANDSCAPE       3   // 270度旋转（竖屏）

// ==================== 颜色定义 (RGB565) ====================
#define RED                 0xF800
#define GREEN               0x07E0
#define BLUE                0x001F
#define WHITE               0xFFFF
#define BLACK               0x0000
#define YELLOW              0xFFE0
#define CYAN                0x07FF
#define MAGENTA             0xF81F

static const char *TAG_TFT = "TFT Display";
static const int TFT_X_OFFSET = 2;
static const int TFT_Y_OFFSET = 3;

// ==================== 函数声明 ====================
void st7735s_init(spi_device_handle_t *spi);
void st7735s_clear(spi_device_handle_t *spi);
void st7735s_fill_screen(spi_device_handle_t *spi, uint16_t color);
void st7735s_send_cmd(spi_device_handle_t *spi, uint8_t cmd);
void st7735s_send_data(spi_device_handle_t *spi, uint8_t data);
void st7735s_delay_ms(uint32_t ms);

#endif