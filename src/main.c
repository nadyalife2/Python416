#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TAG "TEST"

void app_main(void)
{
    ESP_LOGI(TAG, "Hello ESP32-S3");
    while (1) {
        ESP_LOGI(TAG, "Hello ESP32-S3");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}