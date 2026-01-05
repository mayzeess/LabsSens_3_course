#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_err.h"

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          GPIO_NUM_13
#define LEDC_CHANNEL            LEDC_CHANNEL_0

#define LEDC_DUTY_RES           LEDC_TIMER_14_BIT // Set duty resolution to 14 bits

void help_print(void)
{
    printf("Control of LED dimmer by host terminal\n\r");
    printf("press '+' or '-' for change duty time\n\r");
    printf("press '<' or '>' for change frequency\n\r");
    printf("press 'i' or 'I' for info about current mode\n\r");
}

void app_main(void)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = 1000,  // Set output frequency at 4 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    float brightness = 0.5;
    const float brightness_step = 0.05f;
    const uint32_t freq[] = {5, 10, 25, 30, 35, 40, 50, 75, 100, 250, 500, 1000, 4000}; // Hz

    int f_length = sizeof(freq) / sizeof(freq[0]);
    int f_ind = f_length - 1;
    
    help_print();
    printf("Frequency = %ld, Duty = %1.2f\n\r", freq[f_ind], brightness);

    uint32_t duty = (1UL << LEDC_DUTY_RES) * brightness;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));

    while(1)
    {
        char c = getchar();
        switch (c)
        {
            case '+':
                brightness += brightness_step;
                break;
            case '-':
                brightness -= brightness_step;
                break;
            case '<':
                f_ind--;
                break;
            case '>':
                f_ind++;
                break;
        }
        if (brightness > 1.0f)
            brightness = 1.0f;
        if (brightness < 0.0f)
            brightness = 0.0f;
        if(f_ind >= f_length)
            f_ind = 0;
        if(f_ind < 0)
            f_ind = f_length - 1;

        switch (c)
        {
            case '+':
            case '-':
                printf("Duty = %1.2f\n\r", brightness);
                duty = (1UL << LEDC_DUTY_RES) * brightness;
                ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty));
                ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
                break;
            case '<':
            case '>':
                printf("Frequency = %ld\n\r", freq[f_ind]);
                ESP_ERROR_CHECK(ledc_set_freq (LEDC_MODE, LEDC_TIMER, freq[f_ind]));
                break;
            case 'i':
            case 'I':
                printf("Frequency = %ld, Duty = %1.2f\n\r", freq[f_ind], brightness);
                break;
            case 'h':
            case 'H':
                help_print();
                break;
            case 'd':
            case 'D':
                printf("LEDC Duty = %ld\n\r", ledc_get_duty(LEDC_MODE, LEDC_CHANNEL)); 
                break;
            case 'f':
            case 'F':
                printf("LEDC Frequency = %ld\n\r", ledc_get_freq(LEDC_MODE, LEDC_CHANNEL)); 
                break;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}