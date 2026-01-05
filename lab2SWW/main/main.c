#include <stdio.h>
#include <math.h>
#include "driver/i2c.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_PIN 10
#define LED_COUNT 64
#define MATRIX_W 8
#define MATRIX_H 8

#define I2C_SDA 5
#define I2C_SCL 14
#define I2C_PORT I2C_NUM_0
#define MPU_ADDR 0x68

static led_strip_handle_t strip;

void mpu6050_write(uint8_t reg, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU_ADDR << 1) | 0, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
}

void mpu6050_read(uint8_t reg, uint8_t *buf, uint8_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU_ADDR << 1) | 0, true);
    i2c_master_write_byte(cmd, reg, true);

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU_ADDR << 1) | 1, true);
    i2c_master_read(cmd, buf, len, I2C_MASTER_LAST_NACK);

    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
}

void mpu6050_init()
{
    mpu6050_write(0x6B, 0);
}

void set_pixel_xy(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x < 0 || x >= MATRIX_W || y < 0 || y >= MATRIX_H) return;
    int index = y * MATRIX_W + x;
    led_strip_set_pixel(strip, index, r, g, b);
}


void draw_horizon(float angleX_deg, float angleY_deg)
{
    for (int i = 0; i < LED_COUNT; i++)
        led_strip_set_pixel(strip, i, 0, 0, 0);

    float pitch = angleX_deg * 0.05;
    float roll  = angleY_deg * 0.05;

    for (int x = 0; x < MATRIX_W; x++)
    {
        float y = (MATRIX_H / 2) + pitch + (x - MATRIX_W / 2) * roll;

        int y_int = (int)y;
        set_pixel_xy(x, y_int, 0, 100, 40);
        set_pixel_xy(x, y_int + 1, 0, 200, 0);
    }
}

void app_main(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_PIN,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000
    };
    led_strip_new_rmt_device(&strip_config, &rmt_config, &strip);

    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000
    };
    i2c_param_config(I2C_PORT, &i2c_cfg);
    i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);

    mpu6050_init();

    uint8_t data[6];

    while (1)
    {
        mpu6050_read(0x3B, data, 6);

        int16_t ax = (data[0] << 8) | data[1];
        int16_t ay = (data[2] << 8) | data[3];

        float ax_g = ax / 16384.0;
        float ay_g = ay / 16384.0;

        float angleX = atan2(ay_g, sqrt(ax_g * ax_g)) * 57.3;
        float angleY = atan2(ax_g, sqrt(ay_g * ay_g)) * 57.3;

        draw_horizon(angleX, angleY);
        led_strip_refresh(strip);

        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
