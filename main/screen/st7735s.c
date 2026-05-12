#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

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
#define TFT_X_OFFSET        2
#define TFT_Y_OFFSET        3

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

// ==================== 延时函数 ====================
void st7735s_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// ==================== 发送命令 ====================
void st7735s_send_cmd(spi_device_handle_t *spi, uint8_t cmd)
{
    if (spi == NULL || *spi == NULL) return;


    gpio_set_level(ST7735S_RS_PIN, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
        .user = (void*)0,
    };
    spi_device_transmit(*spi, &t);
}

// ==================== 发送数据 ====================
void st7735s_send_data(spi_device_handle_t *spi, uint8_t data)
{
    if (spi == NULL || *spi == NULL) return;

    gpio_set_level(ST7735S_RS_PIN, 1);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &data,
        .user = (void*)1,
    };
    spi_device_transmit(*spi, &t);
}

// ==================== 设置显示窗口 ====================
static void st7735s_set_address_window(spi_device_handle_t *spi, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    x1 += TFT_X_OFFSET;
    x2 += TFT_X_OFFSET;
    y1 += TFT_Y_OFFSET;
    y2 += TFT_Y_OFFSET;

    st7735s_send_cmd(spi, 0x2A);
    st7735s_send_data(spi, (x1 >> 8) & 0xFF);
    st7735s_send_data(spi, x1 & 0xFF);
    st7735s_send_data(spi, (x2 >> 8) & 0xFF);
    st7735s_send_data(spi, x2 & 0xFF);

    st7735s_send_cmd(spi, 0x2B);
    st7735s_send_data(spi, (y1 >> 8) & 0xFF);
    st7735s_send_data(spi, y1 & 0xFF);
    st7735s_send_data(spi, (y2 >> 8) & 0xFF);
    st7735s_send_data(spi, y2 & 0xFF);
}

void lcd_spi_pre_transfer_callback(spi_transaction_t *t)
{
    int dc = (int)t->user;
    gpio_set_level(ST7735S_RS_PIN, dc);
}

// ==================== 初始化（完全按示例：SCK先置0） ====================
void st7735s_init(spi_device_handle_t *spi)
{
    esp_err_t ret;

    // 初始化 GPIO
    gpio_config_t gpio_conf = {
        .pin_bit_mask =
            (1ULL << ST7735S_PWR_PIN)   |
            (1ULL << ST7735S_RST_PIN)   |
            (1ULL << ST7735S_RS_PIN)    |
            (1ULL << ST7735S_SPI4W_PIN) |
            (1ULL << ST7735S_SCL_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&gpio_conf);

    // 上电
    gpio_set_level(ST7735S_PWR_PIN, 1);
    st7735s_delay_ms(10);

    // ===================== 示例代码标准流程 =====================
    gpio_set_level(ST7735S_SCL_PIN, 0);  // SPI_SCK_0
    gpio_set_level(ST7735S_RST_PIN, 0);  // SPI_RST_0
    st7735s_delay_ms(1000);
    gpio_set_level(ST7735S_RST_PIN, 1);  // SPI_RST_1
    st7735s_delay_ms(1000);

    // 4线SPI模式
    gpio_set_level(ST7735S_SPI4W_PIN, 0);

    // SPI 总线初始化
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = ST7735S_SDA_PIN,
        .sclk_io_num = ST7735S_SCL_PIN,
        .miso_io_num = -1,
    };
    ret = spi_bus_initialize(ST7735S_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_TFT, "SPI bus init failed");
        *spi = NULL;
        return;
    }

    // CS 硬件自动控制
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = ST7735S_CS_PIN,
        .queue_size = 8,
        .pre_cb = lcd_spi_pre_transfer_callback,
    };
    ret = spi_bus_add_device(ST7735S_SPI_HOST, &dev_cfg, spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_TFT, "SPI device add failed");
        *spi = NULL;
        return;
    }

    // ==================== 屏幕初始化命令 ====================
    st7735s_send_cmd(spi, 0x11); // Sleep Out
    st7735s_delay_ms(120);

    st7735s_send_cmd(spi, 0xB1);
    st7735s_send_data(spi, 0x01);
    st7735s_send_data(spi, 0x2C);
    st7735s_send_data(spi, 0x2D);

    st7735s_send_cmd(spi, 0xB2);
    st7735s_send_data(spi, 0x01);
    st7735s_send_data(spi, 0x2C);
    st7735s_send_data(spi, 0x2D);

    st7735s_send_cmd(spi, 0xB3);
    st7735s_send_data(spi, 0x01);
    st7735s_send_data(spi, 0x2C);
    st7735s_send_data(spi, 0x2D);
    st7735s_send_data(spi, 0x01);
    st7735s_send_data(spi, 0x2C);
    st7735s_send_data(spi, 0x2D);

    st7735s_send_cmd(spi, 0xB4);
    st7735s_send_data(spi, 0x07);

    st7735s_send_cmd(spi, 0xC0);
    st7735s_send_data(spi, 0xA2);
    st7735s_send_data(spi, 0x02);
    st7735s_send_data(spi, 0x84);

    st7735s_send_cmd(spi, 0xC1);
    st7735s_send_data(spi, 0xC5);

    st7735s_send_cmd(spi, 0xC2);
    st7735s_send_data(spi, 0x0A);
    st7735s_send_data(spi, 0x00);

    st7735s_send_cmd(spi, 0xC3);
    st7735s_send_data(spi, 0x8A);
    st7735s_send_data(spi, 0x2A);

    st7735s_send_cmd(spi, 0xC4);
    st7735s_send_data(spi, 0x8A);
    st7735s_send_data(spi, 0xEE);

    st7735s_send_cmd(spi, 0xC5);
    st7735s_send_data(spi, 0x0E);

    st7735s_send_cmd(spi, 0x36);
    st7735s_send_data(spi, 0xC8);

    st7735s_send_cmd(spi, 0xE0);
    st7735s_send_data(spi, 0x0F);
    st7735s_send_data(spi, 0x1a);
    st7735s_send_data(spi, 0x0f);
    st7735s_send_data(spi, 0x18);
    st7735s_send_data(spi, 0x2f);
    st7735s_send_data(spi, 0x28);
    st7735s_send_data(spi, 0x20);
    st7735s_send_data(spi, 0x22);
    st7735s_send_data(spi, 0x1f);
    st7735s_send_data(spi, 0x1b);
    st7735s_send_data(spi, 0x23);
    st7735s_send_data(spi, 0x37);
    st7735s_send_data(spi, 0x00);
    st7735s_send_data(spi, 0x07);
    st7735s_send_data(spi, 0x02);
    st7735s_send_data(spi, 0x10);

    st7735s_send_cmd(spi, 0xE1);
    st7735s_send_data(spi, 0x0f);
    st7735s_send_data(spi, 0x1b);
    st7735s_send_data(spi, 0x0f);
    st7735s_send_data(spi, 0x17);
    st7735s_send_data(spi, 0x33);
    st7735s_send_data(spi, 0x2c);
    st7735s_send_data(spi, 0x29);
    st7735s_send_data(spi, 0x2e);
    st7735s_send_data(spi, 0x30);
    st7735s_send_data(spi, 0x30);
    st7735s_send_data(spi, 0x39);
    st7735s_send_data(spi, 0x3f);
    st7735s_send_data(spi, 0x00);
    st7735s_send_data(spi, 0x07);
    st7735s_send_data(spi, 0x03);
    st7735s_send_data(spi, 0x10);

    st7735s_send_cmd(spi, 0x2a); // Column address set
    st7735s_send_data(spi, 0x00);
    st7735s_send_data(spi, 0x00+2);
    st7735s_send_data(spi, 0x00);
    st7735s_send_data(spi, 0x80+2);

    st7735s_send_cmd(spi, 0x2b); // Row address set
    st7735s_send_data(spi, 0x00);
    st7735s_send_data(spi, 0x00+3);
    st7735s_send_data(spi, 0x00);
    st7735s_send_data(spi, 0x80+3);

    st7735s_send_cmd(spi, 0xF6); // Disable ram power save mode
    st7735s_send_data(spi, 0x00);

    st7735s_send_cmd(spi, 0x3A);
    st7735s_send_data(spi, 0x05);

    st7735s_send_cmd(spi, 0x20);
    st7735s_send_cmd(spi, 0x29);

    ESP_LOGI(TAG_TFT, "✅ ST7735S initialized OK");
}

// ==================== 全屏填充颜色 ====================
void st7735s_fill_screen(spi_device_handle_t *spi, uint16_t color)
{
    if (spi == NULL || *spi == NULL) return;

    uint8_t buf[TFT_COLUMN_NUMBER * 2];
    for (int i = 0; i < TFT_COLUMN_NUMBER; i++) {
        buf[i*2]   = (color >> 8) & 0xFF;
        buf[i*2+1] = color & 0xFF;
    }

    st7735s_set_address_window(spi, 0, 0, TFT_COLUMN_NUMBER-1, TFT_LINE_NUMBER-1);
    st7735s_send_cmd(spi, 0x2C);

    gpio_set_level(ST7735S_RS_PIN, 1);
    spi_transaction_t t = {
        .length = TFT_COLUMN_NUMBER * 16,
        .tx_buffer = buf,
    };

    for (int y = 0; y < TFT_LINE_NUMBER; y++) {
        spi_device_transmit(*spi, &t);
    }
}

// ==================== 清屏（黑色） ====================
void st7735s_clear(spi_device_handle_t *spi)
{
    st7735s_fill_screen(spi, BLACK);
}