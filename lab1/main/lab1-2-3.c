#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_err.h"

#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_GPIO_R         GPIO_NUM_12
#define LEDC_CHANNEL_R      LEDC_CHANNEL_0
#define LEDC_GPIO_G         GPIO_NUM_13
#define LEDC_CHANNEL_G      LEDC_CHANNEL_1
#define LEDC_GPIO_B         GPIO_NUM_11
#define LEDC_CHANNEL_B      LEDC_CHANNEL_2

#define LEDC_DUTY_RES       LEDC_TIMER_13_BIT
#define LEDC_FADE_TIME      1000

#define RGB_TO_DUTY(x)  ((x) * (1 << LEDC_DUTY_RES) / 255)

typedef struct {
    uint8_t r, g, b;
} rgb_t;

// Набор цветов
static const rgb_t colors[] = {
    {255,   0,   0}, // Красный
    {255, 127,   0}, // Оранжевый
    {255, 255,   0}, // Жёлтый
    {  0, 255,   0}, // Зелёный
    {  0, 255, 255}, // Голубой
    {  0,   0, 255}, // Синий
    {255,   0, 255}  // Фиолетовый
};
#define COLOR_COUNT (sizeof(colors) / sizeof(colors[0]))

static void set_rgb(const rgb_t *c,
                    ledc_channel_config_t *ch_r,
                    ledc_channel_config_t *ch_g,
                    ledc_channel_config_t *ch_b)
{
    ledc_set_fade_with_time(ch_r->speed_mode, ch_r->channel, RGB_TO_DUTY(c->r), LEDC_FADE_TIME);
    ledc_set_fade_with_time(ch_g->speed_mode, ch_g->channel, RGB_TO_DUTY(c->g), LEDC_FADE_TIME);
    ledc_set_fade_with_time(ch_b->speed_mode, ch_b->channel, RGB_TO_DUTY(c->b), LEDC_FADE_TIME);

    ledc_fade_start(ch_r->speed_mode, ch_r->channel, LEDC_FADE_WAIT_DONE);
    ledc_fade_start(ch_g->speed_mode, ch_g->channel, LEDC_FADE_WAIT_DONE);
    ledc_fade_start(ch_b->speed_mode, ch_b->channel, LEDC_FADE_WAIT_DONE);
}

void app_main(void)
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = 4000,
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ch_r = {
        .channel    = LEDC_CHANNEL_R,
        .duty       = 0,
        .gpio_num   = LEDC_GPIO_R,
        .speed_mode = LEDC_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER,
        .flags.output_invert = 0
    };
    ledc_channel_config_t ch_g = {
        .channel    = LEDC_CHANNEL_G,
        .duty       = 0,
        .gpio_num   = LEDC_GPIO_G,
        .speed_mode = LEDC_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER,
        .flags.output_invert = 0
    };
    ledc_channel_config_t ch_b = {
        .channel    = LEDC_CHANNEL_B,
        .duty       = 0,
        .gpio_num   = LEDC_GPIO_B,
        .speed_mode = LEDC_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER,
        .flags.output_invert = 0
    };
    ledc_channel_config(&ch_r);
    ledc_channel_config(&ch_g);
    ledc_channel_config(&ch_b);

    ledc_fade_func_install(0);

    uint8_t state = 0;
    while (1)
    {
        set_rgb(&colors[state], &ch_r, &ch_g, &ch_b);
        state = (state + 1) % COLOR_COUNT;
    }
}
