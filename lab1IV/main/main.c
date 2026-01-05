#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define LED GPIO_NUM_12 // указываем номер GPIO к которому подключен светодиод

void app_main(void)
{
  gpio_set_direction(LED, GPIO_MODE_OUTPUT); // указываем режим работы GPIO
  uint32_t led_on = 0;
  while (true)
  {
    led_on = !led_on;
    gpio_set_level(LED, led_on); // устанавливаем значение на выход GPIO: hight или low (0 или 1)
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}