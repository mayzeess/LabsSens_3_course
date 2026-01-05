#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// пины подключения светодиодов
#define     LED_RED     12
#define     LED_YELLOW  13
#define     LED_GREEN   11
#define     GPIO_PINS   ((1ULL << LED_RED) | (1ULL << LED_YELLOW) | (1ULL << LED_GREEN))

void app_main(void)
{
    // объявление структуры конфигурации
    gpio_config_t io_conf = {};

    // задание необходимых свойств
    io_conf.pin_bit_mask = GPIO_PINS;             // порты
    io_conf.mode = GPIO_MODE_OUTPUT;              // режим работы - выход
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;     // нет подтягивающего резистора
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; // нет стягивающего резистора
    io_conf.intr_type = GPIO_INTR_DISABLE;        // прерывания отключены

    // установка конфигурации портов
    gpio_config(&io_conf);

    while (1)
    {
        gpio_set_level(LED_RED, 1);
        gpio_set_level(LED_YELLOW, 0);
        gpio_set_level(LED_GREEN, 0);
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        gpio_set_level(LED_RED, 1);
        gpio_set_level(LED_YELLOW, 1);
        gpio_set_level(LED_GREEN, 0);
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        gpio_set_level(LED_RED, 0);
        gpio_set_level(LED_YELLOW, 0);
        gpio_set_level(LED_GREEN, 1);
        vTaskDelay(2000 / portTICK_PERIOD_MS);

        for (int i = 0; i < 3; i++) {
            gpio_set_level(LED_GREEN, 0);
            vTaskDelay(300 / portTICK_PERIOD_MS);
            gpio_set_level(LED_GREEN, 1);
            vTaskDelay(300 / portTICK_PERIOD_MS);
        }

        gpio_set_level(LED_RED, 0);
        gpio_set_level(LED_YELLOW, 1);
        gpio_set_level(LED_GREEN, 0);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}