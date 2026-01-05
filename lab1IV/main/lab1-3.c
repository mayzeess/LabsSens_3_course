#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#define LED_RED    GPIO_NUM_12   // Красный светодиод (дверь закрыта)
#define LED_GREEN  GPIO_NUM_11   // Зеленый светодиод (мигает при открытии)
#define RELAY      GPIO_NUM_17   // Реле (замок)
#define BTN_OK     GPIO_NUM_18   // Кнопка подтверждения

static const char *TAG = "LOCK";
const char CORRECT_CODE[] = "1234";

#define BUF_SIZE 32
#define TIMEOUT_MS 10000  // 10 секунд таймаут

void lock_open()
{
    gpio_set_level(RELAY, 1);
    gpio_set_level(LED_RED, 0);

    for (int i = 0; i < 20; i++) {  // 10 секунд мигания
        gpio_set_level(LED_GREEN, i % 2);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    gpio_set_level(RELAY, 0);
    gpio_set_level(LED_GREEN, 0);
    gpio_set_level(LED_RED, 1);
}

void app_main(void)
{
    gpio_set_direction(LED_RED, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_GREEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(RELAY, GPIO_MODE_OUTPUT);
    gpio_set_direction(BTN_OK, GPIO_MODE_INPUT);
    gpio_pullup_en(BTN_OK);

    gpio_set_level(RELAY, 0);
    gpio_set_level(LED_RED, 1);
    gpio_set_level(LED_GREEN, 0);

    char buf[BUF_SIZE] = {0};
    int buf_index = 0;
    int btn_prev = 1; // предыдущее состояние кнопки
    int entering = 0; // флаг, что пользователь начал ввод
    int64_t start_time = 0;

    printf("Введите пароль и нажмите Enter, затем кнопку:\n");
    fflush(stdout);

    while (1) {
        int c = getchar(); // читаем символ
        int64_t now = esp_timer_get_time() / 1000; // время в мс

        if (c != EOF) {
            if (!entering) {
                entering = 1;
                start_time = now; // запускаем таймер при первом символе
            }

            if (c == '\r' || c == '\n') {
                buf[buf_index] = '\0';
                printf("\nПароль введён, нажмите кнопку подтверждения\n");
                fflush(stdout);
            } else if (buf_index < BUF_SIZE - 1) {
                buf[buf_index++] = (char)c;
                printf("*"); // выводим звёздочку
                fflush(stdout);
            }
        }

        // --- Таймер сброса ---
        if (entering && now - start_time >= TIMEOUT_MS) {
            printf("\nВремя истекло! Пароль сброшен\n");
            fflush(stdout);
            buf_index = 0;
            buf[0] = '\0';
            entering = 0;
        }

        // --- Проверка кнопки ---
        int btn_now = gpio_get_level(BTN_OK);
        if (btn_prev == 1 && btn_now == 0) { // фронт кнопки
            vTaskDelay(50 / portTICK_PERIOD_MS); // антидребезг
            if (buf_index > 0) {
                if (strcmp(buf, CORRECT_CODE) == 0) {
                    printf("\nПароль верный! Открываю дверь\n");
                    fflush(stdout);
                    lock_open();
                } else {
                    printf("\nНеверный пароль! Дверь закрыта\n");
                    fflush(stdout);
                }
                buf_index = 0;
                buf[0] = '\0';
                entering = 0;
            } else {
                printf("\nПароль ещё не введён!\n");
                fflush(stdout);
            }
        }
        btn_prev = btn_now;

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
