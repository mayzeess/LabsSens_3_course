#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gptimer.h"

static const char *TAG = "MICROPHONE";

#define SAMPLE_RATE_HZ       2000 
#define BASELINE_SAMPLES     3000
#define ADC_CHANNEL          ADC_CHANNEL_0
#define THRESHOLD            120
#define PRINT_INTERVAL_MS    100

static adc_oneshot_unit_handle_t adc_handle;
static gptimer_handle_t gptimer = NULL;
static int baseline = 0;

static SemaphoreHandle_t data_mutex;
static float filtered_value = 0.0f;
static float amplitude = 0.0f;
static float time_over_threshold_ms = 0.0f;

static float iir_lowpass(float input)
{
    float fs = SAMPLE_RATE_HZ;
    float fc = 250.0f;
    static float prev = 0;
    float alpha = expf(-2.0f * M_PI * fc / fs);
    float y = alpha * prev + (1 - alpha) * input;
    prev = y;
    return y;
}

static bool IRAM_ATTR gptimer_callback(gptimer_handle_t timer, 
                                       const gptimer_alarm_event_data_t *edata, 
                                       void *user_data)
{
    static uint32_t sample_count = 0;
    
    int raw = 0;
    adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw);
    
    int centered = raw - baseline;
    
    float current_filtered = iir_lowpass((float)centered);
    
    if (xSemaphoreTakeFromISR(data_mutex, NULL) == pdTRUE) {
        filtered_value = current_filtered;
        amplitude = fabsf(current_filtered);
        
        if (amplitude > THRESHOLD) {
            time_over_threshold_ms += 1000.0f / SAMPLE_RATE_HZ;
        } else {
            time_over_threshold_ms = 0;
        }
        
        xSemaphoreGiveFromISR(data_mutex, NULL);
    }
    
    sample_count++;
    return true;
}

void print_task(void *arg)
{
    uint32_t last_print = 0;
    
    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        if (current_time - last_print >= PRINT_INTERVAL_MS) {
            last_print = current_time;
            
            float current_amplitude, current_time_over;
            
            if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                current_amplitude = amplitude;
                current_time_over = time_over_threshold_ms;
                xSemaphoreGive(data_mutex);
                
                printf("Amplitude (up to 250 Hz): %.1f | Time > threshold: %.1f ms\n",
                       current_amplitude, current_time_over);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting the lab work...");
    
    data_mutex = xSemaphoreCreateMutex();
    if (data_mutex == NULL) {
        ESP_LOGE(TAG, "error");
        return;
    }
    
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));
    
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_11,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg));
    
    ESP_LOGI(TAG, "Calibration (%d samples)...", BASELINE_SAMPLES);
    
    long sum = 0;
    for (int i = 0; i < BASELINE_SAMPLES; i++) {
        int val = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &val));
        sum += val;
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    baseline = sum / BASELINE_SAMPLES;
    ESP_LOGI(TAG, "Bias level (baseline) = %d", baseline);
    
    ESP_LOGI(TAG, "Setting the timer for sampling frequency %d Hz", SAMPLE_RATE_HZ);
    
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));
    
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 1000000 / SAMPLE_RATE_HZ,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));
    
    gptimer_event_callbacks_t cbs = {
        .on_alarm = gptimer_callback,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
    
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
    
    ESP_LOGI(TAG, "The timer has started...");
    
    xTaskCreate(print_task, "print_task", 2048, NULL, 2, NULL);
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}