#include "st7735s.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ==================== 延时函数 ====================
void st7735s_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// ==================== 4线SPI：发送命令 ====================
void st7735s_send_cmd(spi_device_handle_t *spi, uint8_t cmd)
{
    uint16_t tx_9bit = (0 << 8) | cmd;  // DC=0 + 8bit命令
    ESP_LOGI(TAG_TFT, "ST7735S send cmd: 0x%04X", tx_9bit);
    spi_transaction_t t = {
        .length = 9,
        .tx_buffer = &tx_9bit,
    };
    spi_device_transmit(*spi, &t);
}

// ==================== 4线SPI：发送数据 ====================
void st7735s_send_data(spi_device_handle_t *spi, uint8_t data)
{
    uint16_t tx_9bit = (1 << 8) | data;  // DC=1 + 8bit数据
    spi_transaction_t t = {
        .length = 9,
        .tx_buffer = &tx_9bit,
    };
    spi_device_transmit(*spi, &t);
}

// ==================== 设置显示窗口 ====================
static void st7735s_set_address_window(spi_device_handle_t *spi)
{
    ESP_LOGI(TAG_TFT, "ST7735S set address window");
    st7735s_send_cmd(spi, 0x2A);
    st7735s_send_data(spi, 0x00 + TFT_X_OFFSET);
    st7735s_send_data(spi, 0x00);
    st7735s_send_data(spi, 0x00 + TFT_COLUMN_NUMBER - 1 + TFT_X_OFFSET);

    st7735s_send_cmd(spi, 0x2B);
    st7735s_send_data(spi, 0x00 + TFT_Y_OFFSET);
    st7735s_send_data(spi, 0x00);
    st7735s_send_data(spi, 0x00 + TFT_LINE_NUMBER - 1 + TFT_Y_OFFSET);

    st7735s_send_cmd(spi, 0x2C);
}

// ==================== 初始化（4线SPI模式） ====================
void st7735s_init(spi_device_handle_t *spi)
{
    esp_err_t ret;

    // 4线SPI：增加 RS(DC) 引脚
    gpio_config_t gpio_conf = {
        .pin_bit_mask =
            (1ULL << ST7735S_PWR_PIN)   |
            (1ULL << ST7735S_RST_PIN)   |
            (1ULL << ST7735S_SPI4W_PIN) |
            (1ULL << ST7735S_RS_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = false,
    };
    gpio_config(&gpio_conf);

    // 上电
    gpio_set_level(ST7735S_PWR_PIN, 1);
    st7735s_delay_ms(100);

    // 硬件复位
    gpio_set_level(ST7735S_SCL_PIN, 0);
    gpio_set_level(ST7735S_RST_PIN, 0);
    st7735s_delay_ms(1000);
    gpio_set_level(ST7735S_RST_PIN, 1);
    st7735s_delay_ms(1000);

    // 4线SPI模式：SPI4W_PIN = 0
    gpio_set_level(ST7735S_SPI4W_PIN, 0);

    // SPI 总线初始化
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = ST7735S_SDA_PIN,
        .sclk_io_num = ST7735S_SCL_PIN,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    ret = spi_bus_initialize(ST7735S_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_TFT, "SPI bus init failed");
        *spi = NULL;
        return;
    }

    // 4线SPI：必须加 pre_cb 回调
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 20 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = ST7735S_CS_PIN,
        .queue_size = 16,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    ret = spi_bus_add_device(ST7735S_SPI_HOST, &dev_cfg, spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_TFT, "SPI device add failed");
        *spi = NULL;
        return;
    }

    // ==================== 屏幕初始化命令（完全不变） ====================
    st7735s_send_cmd(spi, 0x11);
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

    st7735s_send_cmd(spi, 0x2A);
    st7735s_send_data(spi, 0x00);
    st7735s_send_data(spi, 0x00+2);
    st7735s_send_data(spi, 0x00);
    st7735s_send_data(spi, 0x80+2);

    st7735s_send_cmd(spi, 0x2B);
    st7735s_send_data(spi, 0x00);
    st7735s_send_data(spi, 0x00+3);
    st7735s_send_data(spi, 0x00);
    st7735s_send_data(spi, 0x80+3);

    st7735s_send_cmd(spi, 0xF0);
    st7735s_send_data(spi, 0x01);
    st7735s_send_cmd(spi, 0xF6);
    st7735s_send_data(spi, 0x00);

    st7735s_send_cmd(spi, 0x3A);
    st7735s_send_data(spi, 0x05);

    st7735s_send_cmd(spi, 0x20);
    st7735s_send_cmd(spi, 0x29);
    st7735s_delay_ms(100);

    ESP_LOGI(TAG_TFT, "ST7735S 4线SPI初始化完成");
}

// ==================== 全屏填充（RTOS优化版） ====================
void st7735s_fill_screen(spi_device_handle_t *spi, uint16_t color)
{
    if (spi == NULL || *spi == NULL) return;

    uint8_t h = (color >> 8) & 0xFF;
    uint8_t l = color & 0xFF;

    st7735s_set_address_window(spi);

    // 使用DMA批量发送数据，减少阻塞时间
    #define ST7735S_BATCH_SIZE 512
    uint16_t batch_buffer[ST7735S_BATCH_SIZE];  // 每个像素9位，用16位存储
    
    // 预填充批次缓冲区：DC=1 + 颜色高字节
    for (int i = 0; i < ST7735S_BATCH_SIZE; i += 2) {
        batch_buffer[i] = (1 << 8) | h;      // DC=1 + 高字节
        batch_buffer[i + 1] = (1 << 8) | l;  // DC=1 + 低字节
    }
    
    uint32_t total_pixels = (uint32_t)TFT_COLUMN_NUMBER * TFT_LINE_NUMBER;
    uint32_t remaining = total_pixels * 2;  // 每个像素2字节
    esp_err_t ret;
    
    while (remaining > 0) {
        uint32_t send_count = (remaining > ST7735S_BATCH_SIZE) ? ST7735S_BATCH_SIZE : remaining;
        
        spi_transaction_t t = {
            .length = send_count * 9,  // 每个元素9位
            .tx_buffer = batch_buffer,
        };
        
        ret = spi_device_transmit(*spi, &t);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_TFT, "Failed to send fill screen data");
            break;
        }
        
        remaining -= send_count;
        
        // 定期让出CPU，防止看门狗超时
        if (remaining > 0) {
            taskYIELD();
        }
    }
}

// ==================== 清屏 ====================
void st7735s_clear(spi_device_handle_t *spi)
{
    st7735s_fill_screen(spi, BLACK);
}