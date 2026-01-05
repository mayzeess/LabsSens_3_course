#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "esp_random.h"

#define TAG "ENCODER_LED"

#define ENC_A GPIO_NUM_2
#define ENC_B GPIO_NUM_3
#define ENC_BTN GPIO_NUM_4
#define LED_GPIO GPIO_NUM_5

#define LED_COUNT 16

static volatile int encoder_value = 5;
static volatile bool start_spin = false;

static led_strip_handle_t led_strip;

static void IRAM_ATTR encoder_isr(void *arg)
{
    static uint8_t last_state = 0;
    uint8_t a = gpio_get_level(ENC_A);
    uint8_t b = gpio_get_level(ENC_B);
    uint8_t state = (a << 1) | b;

    if ((last_state == 0b00 && state == 0b01) ||

    
        (last_state == 0b01 && state == 0b11) ||
        (last_state == 0b11 && state == 0b10) ||
        (last_state == 0b10 && state == 0b00)) {
        encoder_value++;
    } else {
        encoder_value--;
    }

    if (encoder_value < 1) encoder_value = 1;
    if (encoder_value > 20) encoder_value = 20;

    last_state = state;
}

static void IRAM_ATTR button_isr(void *arg)
{
    start_spin = true;
}

static void show_pixel(int pos)
{
    led_strip_clear(led_strip);
    led_strip_set_pixel(led_strip, pos, 255, 50, 0);
    led_strip_refresh(led_strip);
}

void app_main(void)
{
    srand(esp_random());

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ENC_A) | (1ULL << ENC_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    gpio_config(&io_conf);

    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << ENC_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&btn_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(ENC_A, encoder_isr, NULL);
    gpio_isr_handler_add(ENC_B, encoder_isr, NULL);
    gpio_isr_handler_add(ENC_BTN, button_isr, NULL);

    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000
    };

    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    led_strip_clear(led_strip);

    ESP_LOGI(TAG, "Ready");

    int pos = 0;

    while (1) {
        if (start_spin) {
            start_spin = false;

            int delay = 50;
            int stop_after = (rand() % 40) + 40;

            for (int i = 0; i < stop_after; i++) {
                pos = (pos + 1) % LED_COUNT;
                show_pixel(pos);

                vTaskDelay(pdMS_TO_TICKS(delay));

                delay += encoder_value;
            }

        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
