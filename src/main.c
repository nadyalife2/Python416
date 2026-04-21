#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <math.h>

#define TAG "AUDIO_TEST"

// Пины Waveshare-платы
#define PA_CTRL       GPIO_NUM_7
#define I2S_MCLK_PIN  GPIO_NUM_8
#define I2S_BCLK_PIN  GPIO_NUM_9
#define I2S_LRCK_PIN  GPIO_NUM_10
#define I2S_DOUT_PIN  GPIO_NUM_11
#define I2S_DIN_PIN   GPIO_NUM_12
#define I2C_SCL_PIN   GPIO_NUM_41
#define I2C_SDA_PIN   GPIO_NUM_42

#define ES8311_ADDR   0x18

static i2s_chan_handle_t tx_chan;

static esp_err_t i2c_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
    return i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
}

static esp_err_t es8311_write(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ES8311_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static void es8311_init_basic(void)
{
    vTaskDelay(pdMS_TO_TICKS(50));
    es8311_write(0x00, 0x3F); // reset
    vTaskDelay(pdMS_TO_TICKS(50));
    es8311_write(0x0B, 0x82); // power up DAC+ADC
    es8311_write(0x0C, 0x4C); // I2S, 16-bit, slave
    es8311_write(0x10, 0x00); // DAC volume mid
}

static void i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_PIN,
            .bclk = I2S_BCLK_PIN,
            .ws   = I2S_LRCK_PIN,
            .dout = I2S_DOUT_PIN,
            .din  = I2S_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
}

void app_main(void)
{
    ESP_LOGI(TAG, "Start audio test");

    // Включаем усилитель
    gpio_set_direction(PA_CTRL, GPIO_MODE_OUTPUT);
    gpio_set_level(PA_CTRL, 1);

    ESP_ERROR_CHECK(i2c_init());
    ESP_LOGI(TAG, "I2C initialized");

    es8311_init_basic();
    ESP_LOGI(TAG, "ES8311 initialized");

    i2s_init();
    ESP_LOGI(TAG, "I2S initialized — generating 1kHz tone");

    const float fs = 16000.0f;
    const float freq = 1000.0f;
    float phase = 0;
    int16_t buf[256 * 2];

    while (1) {
        for (int i = 0; i < 256; ++i) {
            float s = sinf(2 * M_PI * freq * phase / fs) * 20000.0f;
            phase += 1.0f;
            int16_t sample = (int16_t)s;
            buf[2 * i]     = sample;
            buf[2 * i + 1] = sample;
        }
        size_t written = 0;
        i2s_channel_write(tx_chan, buf, sizeof(buf), &written, pdMS_TO_TICKS(1000));
    }
}