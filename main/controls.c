#include <assert.h>
#include <stdio.h>
#include "sensecap-watcher.h"
#include "sensitivity.h"
#include "controls.h"

static portMUX_TYPE gain_lock = portMUX_INITIALIZER_UNLOCKED;
static int gain_db = SENSITIVITY_DEFAULT_DB;
static esp_io_expander_handle_t expander;
static knob_handle_t knob;
static button_handle_t button;

static void change_gain(sensitivity_event_t event) {
  portENTER_CRITICAL(&gain_lock);
  gain_db = sensitivity_change(gain_db, event);
  portEXIT_CRITICAL(&gain_lock);
}

static void turn_left(void *handle, void *context) {
  change_gain(SENSITIVITY_DOWN);
}

static void turn_right(void *handle, void *context) {
  change_gain(SENSITIVITY_UP);
}

static void reset_gain(void *handle, void *context) {
  change_gain(SENSITIVITY_RESET);
}

static uint8_t button_level(void *context) {
  uint32_t value;
  // Read failures are treated as released, never as a reset request.
  if (esp_io_expander_get_level(expander, BSP_KNOB_BTN, &value) != ESP_OK) return 1;
  return (value & BSP_KNOB_BTN) != 0;
}

void controls_init(void) {
  // LVGL's encoder is disabled: these callbacks own the physical knob.
  expander = bsp_io_expander_init();
  assert(expander);
  uint32_t value;
  ESP_ERROR_CHECK(esp_io_expander_get_level(expander, BSP_KNOB_BTN, &value));
  const knob_config_t knob_config = {
    .default_direction = 0,
    .gpio_encoder_a = BSP_KNOB_A,
    .gpio_encoder_b = BSP_KNOB_B,
  };
  knob = iot_knob_create(&knob_config);
  assert(knob);
  ESP_ERROR_CHECK(iot_knob_register_cb(knob, KNOB_LEFT, turn_left, NULL));
  ESP_ERROR_CHECK(iot_knob_register_cb(knob, KNOB_RIGHT, turn_right, NULL));
  const button_config_t button_config = {
    .type = BUTTON_TYPE_CUSTOM,
    .long_press_time = 2000,
    .short_press_time = 200,
    .custom_button_config = {
      .active_level = 0,
      .button_custom_get_key_value = button_level,
      // The I/O expander is shared with power control; never delete it here.
    },
  };
  button = iot_button_create(&button_config);
  assert(button);
  ESP_ERROR_CHECK(iot_button_register_cb(button, BUTTON_SINGLE_CLICK, reset_gain, NULL));
  puts("SPECTRUM_CONTROLS_READY_V6");
}

int controls_gain_db(void) {
  portENTER_CRITICAL(&gain_lock);
  int value = gain_db;
  portEXIT_CRITICAL(&gain_lock);
  return value;
}
