#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <math.h>

#define TAG "SAY_AUDIO"

// ============================================================
// SpotPear ESP32-S3-1.54 V2.0 — ПОДТВЕРЖДЁННАЯ распиновка
// ES8311 найден I2C сканом на SCL=14, SDA=15
// ============================================================

// I2C (подтверждено сканом!)
#define I2C_SCL_PIN    GPIO_NUM_14
#define I2C_SDA_PIN    GPIO_NUM_15

// I2S — вариант A: оригинальные «проверенные» пины пользователя
#define I2S_MCLK_PIN   GPIO_NUM_12
#define I2S_BCLK_PIN   GPIO_NUM_8
#define I2S_LRCK_PIN   GPIO_NUM_16  // WS
#define I2S_DOUT_PIN   GPIO_NUM_7   // ESP32 → ES8311 (воспроизведение)
#define I2S_DIN_PIN    GPIO_NUM_NC  // микрофон пока не нужен

// Усилитель NS4150B (strapping pin — нужна мягкая инициализация!)
#define PA_CTRL        GPIO_NUM_46

#define ES8311_ADDR    0x18

static i2s_chan_handle_t tx_chan;

// ============================================================
// I2C
// ============================================================
static esp_err_t i2c_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_SDA_PIN,
        .scl_io_num       = I2C_SCL_PIN,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
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
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "ES8311 reg 0x%02X FAIL: %s", reg, esp_err_to_name(ret));
    else
        ESP_LOGI(TAG, "  ES8311 reg 0x%02X = 0x%02X OK", reg, val);
    return ret;
}

// I2C скан
static void i2c_scan(void)
{
    ESP_LOGI(TAG, "I2C скан (SCL=%d, SDA=%d)...", I2C_SCL_PIN, I2C_SDA_PIN);
    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(20));
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  -> 0x%02X %s", addr,
                     (addr == 0x18) ? "(ES8311!)" : "");
            found++;
        }
    }
    ESP_LOGI(TAG, "Найдено устройств: %d", found);
}

// ============================================================
// ES8311 init — полная инициализация DAC
// ============================================================
static void es8311_init(void)
{
    ESP_LOGI(TAG, "ES8311 инициализация...");

    // Сброс
    es8311_write(0x00, 0x80);
    vTaskDelay(pdMS_TO_TICKS(20));
    es8311_write(0x00, 0x00);
    vTaskDelay(pdMS_TO_TICKS(20));

    // Тактирование — MCLK внешний, divider /1
    es8311_write(0x01, 0x3F);
    es8311_write(0x02, 0x00);
    es8311_write(0x03, 0x10);
    es8311_write(0x04, 0x10);
    es8311_write(0x05, 0x00);

    // Питание
    es8311_write(0x0B, 0x00);
    es8311_write(0x0C, 0x00);
    es8311_write(0x0D, 0x01);
    es8311_write(0x0E, 0x02);

    // I2S: Slave, Philips, 16 бит
    es8311_write(0x09, 0x00);
    es8311_write(0x0A, 0x00);

    // DAC путь
    es8311_write(0x12, 0x00);
    es8311_write(0x13, 0x00);

    // Громкость + unmute
    es8311_write(0x32, 0xBF);
    es8311_write(0x31, 0x00);

    // Микшер → выход
    es8311_write(0x37, 0x08);
    es8311_write(0x45, 0x00);

    ESP_LOGI(TAG, "========== ES8311 ГОТОВ ==========");
}

// ============================================================
// I2S — Master, 16kHz, 16bit, stereo, MCLK=256*fs
// ============================================================
static void i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT,
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
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
    ESP_LOGI(TAG, "I2S готов (MCLK=%d, BCLK=%d, WS=%d, DOUT=%d, DIN=%d)",
             I2S_MCLK_PIN, I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN, I2S_DIN_PIN);
}

// ============================================================
// app_main
// ============================================================
void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "=============================================");
    ESP_LOGI(TAG, "  SpotPear ESP32-S3-1.54 V2.0 — Audio Test");
    ESP_LOGI(TAG, "  I2C: SCL=14, SDA=15 (подтверждено!)");
    ESP_LOGI(TAG, "  I2S: MCLK=16, BCLK=9, WS=45, DOUT=10, DIN=11");
    ESP_LOGI(TAG, "  PA: GPIO46 (NS4150B)");
    ESP_LOGI(TAG, "=============================================");

    // 1. I2C
    ESP_ERROR_CHECK(i2c_init());
    i2c_scan();

    // 2. I2S (MCLK нужен ДО ES8311 init!)
    i2s_init();
    vTaskDelay(pdMS_TO_TICKS(50));

    // 3. ES8311
    es8311_init();

    // 4. Безопасное включение усилителя NS4150B
    //    GPIO46 — strapping pin, нужна мягкая инициализация!
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_reset_pin(PA_CTRL);
    gpio_set_direction(PA_CTRL, GPIO_MODE_OUTPUT);
    gpio_set_level(PA_CTRL, 1);
    ESP_LOGI(TAG, "NS4150B PA включён (GPIO46=HIGH)");

    // 5. Синус 1 кГц
    ESP_LOGI(TAG, ">>> Генерация 1 кГц синуса — СЛУШАЙ ДИНАМИК! <<<");

    const int   SAMPLES   = 256;
    const float FS        = 16000.0f;
    const float FREQ      = 1000.0f;
    const float AMP       = 20000.0f;
    const float PHASE_INC = FREQ / FS;

    int16_t buf[SAMPLES * 2];
    float   phase = 0.0f;

    while (1) {
        for (int i = 0; i < SAMPLES; i++) {
            int16_t s = (int16_t)(sinf(2.0f * (float)M_PI * phase) * AMP);
            phase += PHASE_INC;
            if (phase >= 1.0f) phase -= 1.0f;
            buf[2 * i]     = s;
            buf[2 * i + 1] = s;
        }
        size_t written = 0;
        i2s_channel_write(tx_chan, buf, sizeof(buf), &written, pdMS_TO_TICKS(1000));
    }
}