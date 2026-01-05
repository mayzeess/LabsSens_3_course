#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "led_strip.h"

#define NUM_PIXELS          37
#define ENCODER_A_GPIO      2
#define ENCODER_B_GPIO      3
#define ENCODER_SW_GPIO     4
#define LED_STRIP_GPIO      5
#define FRAME_MS            20
#define DT_SEC              (FRAME_MS / 1000.0f)
#define SPEED_MIN           1
#define SPEED_MAX           80
#define SPEED_SCALE         1.6f
#define WHEEL_DAMP          0.20f
#define BALL_COUPLING       1.30f
#define BALL_DAMP           0.35f
#define MIN_SPIN_TIME       2.3f
#define LOCK_DIFF_THRESH    0.70f
#define STOP_SPEED_THRESH   0.12f

static const char *TAG = "roulette";

static const int wheel_numbers[NUM_PIXELS] = {
  0, 32, 15, 19, 4, 21, 2, 25, 17, 34,
  6, 27, 13, 36, 11, 30, 8, 23, 10, 5,
  24, 16, 33, 1, 20, 14, 31, 9, 22, 18,
  29, 7, 28, 12, 35, 3, 26
};

typedef enum { SLOT_BLACK = 0, SLOT_RED = 1, SLOT_GREEN = 2 } slot_color_t;

static const slot_color_t wheel_colors[NUM_PIXELS] = {
  SLOT_GREEN, SLOT_RED, SLOT_BLACK, SLOT_RED, SLOT_BLACK, SLOT_RED, SLOT_BLACK, SLOT_RED, SLOT_BLACK, SLOT_RED,
  SLOT_BLACK, SLOT_RED, SLOT_BLACK, SLOT_RED, SLOT_BLACK, SLOT_RED, SLOT_BLACK, SLOT_RED, SLOT_BLACK, SLOT_RED,
  SLOT_BLACK, SLOT_RED, SLOT_BLACK, SLOT_RED, SLOT_BLACK, SLOT_RED, SLOT_BLACK, SLOT_RED, SLOT_BLACK, SLOT_RED,
  SLOT_BLACK, SLOT_RED, SLOT_BLACK, SLOT_RED, SLOT_BLACK, SLOT_RED, SLOT_BLACK
};

static led_strip_handle_t strip = NULL;

static volatile int32_t g_encoder_delta = 0;
static volatile bool g_start_pressed = false;

static volatile uint8_t g_enc_prev = 0;
static portMUX_TYPE g_enc_mux = portMUX_INITIALIZER_UNLOCKED;
static const int8_t ENC_LUT[16] = {
  0, -1,  1,  0,
  1,  0,  0, -1,
 -1,  0,  0,  1,
  0,  1, -1,  0
};

static inline uint8_t enc_read_state(void) {
  int a = gpio_get_level(ENCODER_A_GPIO);
  int b = gpio_get_level(ENCODER_B_GPIO);
  return (uint8_t)((a << 1) | b);
}

static void IRAM_ATTR encoder_isr(void *arg) {
  uint8_t curr = enc_read_state();
  uint8_t prev = g_enc_prev;
  uint8_t idx  = (uint8_t)((prev << 2) | curr);
  int8_t step  = ENC_LUT[idx];

  if (step != 0) {
    portENTER_CRITICAL_ISR(&g_enc_mux);
    g_encoder_delta += step;
    g_enc_prev = curr;
    portEXIT_CRITICAL_ISR(&g_enc_mux);
  } else {
    g_enc_prev = curr;
  }
}

static void IRAM_ATTR button_isr(void *arg) {
  g_start_pressed = true;
}

static int g_speed_setting = 25;

static bool g_spinning = false;
static bool g_locked = false;
static bool g_result_printed = false;

static float wheel_phase = 0.0f;
static float ball_phase  = 0.0f;
static float wheel_speed = 0.0f;
static float ball_speed  = 0.0f;

static float spin_time   = 0.0f;
static int ball_offset_slot = 0;

static inline int mod_int(int a, int n) {
  int r = a % n;
  return (r < 0) ? (r + n) : r;
}

static void set_slot_color(int pixel, slot_color_t c) {
  if (c == SLOT_BLACK) {
    led_strip_set_pixel(strip, pixel, 0, 0, 0);
    return;
  }
  if (c == SLOT_RED) {
    led_strip_set_pixel(strip, pixel, 255, 0, 0);
    return;
  }
  led_strip_set_pixel(strip, pixel, 0, 255, 0);
}

static void render_frame(void) {
  int wheel_index = mod_int((int)floorf(wheel_phase), NUM_PIXELS);

  for (int i = 0; i < NUM_PIXELS; i++) {
    int base_slot = mod_int(i - wheel_index, NUM_PIXELS);
    set_slot_color(i, wheel_colors[base_slot]);
  }

  int ball_pixel = 0;
  if (g_locked) {
    ball_pixel = mod_int(wheel_index + ball_offset_slot, NUM_PIXELS);
  } else {
    ball_pixel = mod_int((int)floorf(ball_phase), NUM_PIXELS);
  }
  led_strip_set_pixel(strip, ball_pixel, 255, 255, 255);

  led_strip_refresh(strip);
}

static void start_spin(void) {
  g_spinning = true;
  g_locked = false;
  g_result_printed = false;

  wheel_phase = 0.0f;
  ball_phase  = 0.0f;
  spin_time   = 0.0f;

  wheel_speed = g_speed_setting * SPEED_SCALE;
  ball_speed  = -wheel_speed * 1.8f;

  ball_offset_slot = (int)(esp_random() % NUM_PIXELS);

  ESP_LOGI(TAG, "START: speed_setting=%d (wheel_speed=%.2f px/s), target_slot=%d (num=%d)",
           g_speed_setting, wheel_speed, ball_offset_slot, wheel_numbers[ball_offset_slot]);
}

static void update_simulation(void) {
  if (!g_spinning) return;

  spin_time += DT_SEC;

  wheel_phase += wheel_speed * DT_SEC;
  if (!g_locked) ball_phase += ball_speed * DT_SEC;

  if (wheel_phase > 1e6f) wheel_phase = fmodf(wheel_phase, (float)NUM_PIXELS);
  if (ball_phase  < -1e6f || ball_phase > 1e6f) ball_phase = fmodf(ball_phase, (float)NUM_PIXELS);

  wheel_speed *= (1.0f - WHEEL_DAMP * DT_SEC);
  if (wheel_speed < 0.0f) wheel_speed = 0.0f;

  if (!g_locked) {
    ball_speed += (wheel_speed - ball_speed) * (BALL_COUPLING * DT_SEC);
    ball_speed *= (1.0f - BALL_DAMP * DT_SEC);

    if (spin_time > MIN_SPIN_TIME && fabsf(ball_speed - wheel_speed) < LOCK_DIFF_THRESH) {
      g_locked = true;
      ball_speed = wheel_speed;
      ESP_LOGI(TAG, "LOCK: slot=%d num=%d", ball_offset_slot, wheel_numbers[ball_offset_slot]);
    }
  } else {
    ball_speed = wheel_speed;
  }

  if (g_locked && wheel_speed < STOP_SPEED_THRESH) {
    wheel_speed = 0.0f;
    ball_speed  = 0.0f;
    g_spinning  = false;

    if (!g_result_printed) {
      ESP_LOGI(TAG, "STOP: result slot=%d number=%d", ball_offset_slot, wheel_numbers[ball_offset_slot]);
      g_result_printed = true;
    }
  }
}

static void init_led_strip(void) {
  led_strip_config_t strip_config = {
    .strip_gpio_num = LED_STRIP_GPIO,
    .max_leds = NUM_PIXELS,
    .led_pixel_format = LED_PIXEL_FORMAT_GRB,
    .led_model = LED_MODEL_WS2812,
    .flags.invert_out = false,
  };

  led_strip_rmt_config_t rmt_config = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 10 * 1000 * 1000,
    .mem_block_symbols = 64,
    .flags.with_dma = false,
  };

  ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip));
  ESP_ERROR_CHECK(led_strip_clear(strip));
}

static void init_gpio(void) {
  gpio_config_t in_cfg = {
    .pin_bit_mask = (1ULL << ENCODER_A_GPIO) | (1ULL << ENCODER_B_GPIO) | (1ULL << ENCODER_SW_GPIO),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&in_cfg));
  g_enc_prev = enc_read_state();

  ESP_ERROR_CHECK(gpio_install_isr_service(0));

  ESP_ERROR_CHECK(gpio_set_intr_type(ENCODER_A_GPIO, GPIO_INTR_ANYEDGE));
  ESP_ERROR_CHECK(gpio_set_intr_type(ENCODER_B_GPIO, GPIO_INTR_ANYEDGE));
  ESP_ERROR_CHECK(gpio_isr_handler_add(ENCODER_A_GPIO, encoder_isr, NULL));
  ESP_ERROR_CHECK(gpio_isr_handler_add(ENCODER_B_GPIO, encoder_isr, NULL));

  ESP_ERROR_CHECK(gpio_set_intr_type(ENCODER_SW_GPIO, GPIO_INTR_NEGEDGE));
  ESP_ERROR_CHECK(gpio_isr_handler_add(ENCODER_SW_GPIO, button_isr, NULL));
}

void app_main(void) {
  ESP_LOGI(TAG, "Roulette simulator start (encoder via GPIO interrupts on A and B)");

  init_led_strip();
  init_gpio();

  ESP_LOGI(TAG, "Rotate encoder to set speed (%d..%d). Press SW to start.", SPEED_MIN, SPEED_MAX);

  while (1) {
    int32_t d = 0;
    portENTER_CRITICAL(&g_enc_mux);
    d = g_encoder_delta;
    g_encoder_delta = 0;
    portEXIT_CRITICAL(&g_enc_mux);

    if (d != 0 && !g_spinning) {
      g_speed_setting += (int)d;
      if (g_speed_setting < SPEED_MIN) g_speed_setting = SPEED_MIN;
      if (g_speed_setting > SPEED_MAX) g_speed_setting = SPEED_MAX;
      ESP_LOGI(TAG, "Speed setting: %d", g_speed_setting);
    }

    if (g_start_pressed) {
      g_start_pressed = false;
      if (!g_spinning) start_spin();
    }

    update_simulation();
    render_frame();
    vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
  }
}
