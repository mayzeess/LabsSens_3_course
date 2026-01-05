#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BUTTON_GPIO 4            // Пин кнопки
#define DEBOUNCE_TIME_MS 50      // Время подавления дребезга в мс

static const char *TAG = "BUTTON";
static volatile uint32_t last_isr_time = 0;

// Обработчик прерывания
static void IRAM_ATTR button_isr_handler(void* arg) {
    uint32_t current_time = xTaskGetTickCountFromISR();
    if ((current_time - last_isr_time) * portTICK_PERIOD_MS > DEBOUNCE_TIME_MS) {
        last_isr_time = current_time;
        // Безопаснее всего использовать printf в ISR
        printf("Button state changed!\n");
    }
}


void app_main(void) {
    // Настройка GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,    // Внешний резистор подтягивает к VCC
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE        // Прерывание на нажатие и отпускание
    };
    gpio_config(&io_conf);

    // Устанавливаем обработчик прерывания
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);

    ESP_LOGI(TAG, "Button initialized on GPIO %d", BUTTON_GPIO);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Основной цикл ничего не делает
    }
}
