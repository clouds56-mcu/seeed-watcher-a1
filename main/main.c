#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include "esp_timer.h"
#include "spectrum.h"
#include "esp_log.h"
#include "sensecap-watcher.h"
#include "sound_level.h"
#include "controls.h"
#include "sensitivity.h"
#include "spectrum_display.h"
#include "ui_format.h"

static float band_levels[SPECTRUM_BANDS];
static spectrum_t spectrum;

static portMUX_TYPE render_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t render_frames, render_ms, render_max_ms;
static uint64_t render_pixels;
static uint64_t fft_us, ui_wait_us;
static uint32_t fft_max_us, ui_wait_max_us;

static void render_monitor(lv_disp_drv_t *driver, uint32_t elapsed_ms, uint32_t pixels) {
  portENTER_CRITICAL(&render_lock);
  ++render_frames;
  render_ms += elapsed_ms;
  if (elapsed_ms > render_max_ms) render_max_ms = elapsed_ms;
  render_pixels += pixels;
  portEXIT_CRITICAL(&render_lock);
}

static void report_performance(int64_t elapsed_us) {
  portENTER_CRITICAL(&render_lock);
  uint32_t frames = render_frames, total_ms = render_ms, max_ms = render_max_ms;
  uint64_t pixels = render_pixels;
  render_frames = render_ms = render_max_ms = 0;
  render_pixels = 0;
  portEXIT_CRITICAL(&render_lock);
  // Only aggregate timing/counts, never PCM, spectrum or input values.
  printf("PERF_V7 elapsed_ms=%lu frames=%lu fft_avg_us=%lu fft_max_us=%lu ui_wait_avg_us=%lu ui_wait_max_us=%lu render_avg_ms=%lu render_max_ms=%lu pixels_avg=%lu\n",
    (unsigned long)(elapsed_us / 1000), (unsigned long)frames,
    (unsigned long)(fft_us / 100), (unsigned long)fft_max_us,
    (unsigned long)(ui_wait_us / 100), (unsigned long)ui_wait_max_us,
    (unsigned long)(frames ? total_ms / frames : 0), (unsigned long)max_ms,
    (unsigned long)(frames ? pixels / frames : 0));
  fft_us = ui_wait_us = 0;
  fft_max_us = ui_wait_max_us = 0;
}


static bool update_ui(float dbfs, float average_dbfs, bool failed, bool no_signal) {
  uint32_t color = spectrum_display_color(dbfs, failed, no_signal);
  static uint32_t previous_color = UINT32_MAX;
  if (color != previous_color) {
    ESP_ERROR_CHECK(bsp_rgb_set((color >> 16 & 255) / 8, (color >> 8 & 255) / 8, (color & 255) / 8));
    previous_color = color;
  }
  int64_t wait_start = esp_timer_get_time();
  bool locked = lvgl_port_lock(100);
  uint32_t wait_us = (uint32_t)(esp_timer_get_time() - wait_start);
  ui_wait_us += wait_us;
  if (wait_us > ui_wait_max_us) ui_wait_max_us = wait_us;
  if (!locked) return false;
  spectrum_display_update(dbfs, average_dbfs, band_levels, controls_gain_db(),
                          failed, no_signal, esp_timer_get_time());
  lvgl_port_unlock();
  return true;
}

void app_main(void) {
  // No network, storage, camera or speech processing is initialized.
  assert(bsp_io_expander_init());
  ESP_ERROR_CHECK(bsp_exp_io_set_level(BSP_PWR_AI_CHIP | BSP_PWR_SDCARD | BSP_PWR_GROVE, 0));
  ESP_ERROR_CHECK(bsp_rgb_init());
  bsp_display_cfg_t display = {
    .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
    .buffer_size = 412 * 40,
    .double_buffer = true,
    .flags = {.buff_dma = true, .buff_spiram = false},
  };
  display.lvgl_port_cfg.task_stack = CONFIG_LVGL_PORT_TASK_STACK_SIZE;
  display.lvgl_port_cfg.task_priority = CONFIG_LVGL_PORT_TASK_PRIORITY;
  display.lvgl_port_cfg.task_affinity = CONFIG_LVGL_PORT_TASK_AFFINITY;
  display.lvgl_port_cfg.timer_period_ms = CONFIG_LVGL_PORT_TIMER_PERIOD_MS;
  display.lvgl_port_cfg.task_max_sleep_ms = CONFIG_LVGL_PORT_TASK_MAX_SLEEP_MS;
  assert(bsp_lvgl_init_with_cfg(&display));
  assert(lvgl_port_lock(0));
  for (size_t i = 0; i < SPECTRUM_BANDS; ++i) band_levels[i] = -90;
  spectrum_display_create(lv_scr_act(), "CLICK: RESET");
  bsp_lvgl_get_disp()->driver->monitor_cb = render_monitor;
  lvgl_port_unlock();
  controls_init();
  char format_check[16];
  ui_format_dbfs(format_check, sizeof(format_check), -421);
  assert(strcmp(format_check, "-42.1") == 0);
  ui_format_dbfs(format_check, sizeof(format_check), -1);
  assert(strcmp(format_check, "-0.1") == 0);
  puts("DISPLAY_FORMAT_READY_V7");
  esp_codec_dev_handle_t mic = bsp_audio_codec_microphone_init();
  // Generic mono defaults to LEFT in esp_codec_dev; the mic is on RIGHT.
  esp_codec_dev_sample_info_t format = {
    .sample_rate = 16000, .channel = 2, .bits_per_sample = 16,
    .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1),
  };
  if (!mic || esp_codec_dev_open(mic, &format) != ESP_CODEC_DEV_OK ||
      esp_codec_dev_set_in_gain(mic, 27.0f) != ESP_CODEC_DEV_OK) {
    update_ui(-90, -90, true, false);
    puts("SOUND_METER_MIC_ERROR");
    return;
  }
  spectrum_init(&spectrum);
  puts("SOUND_METER_READY_V7");
  int16_t samples[800]; // 50 ms, reused immediately; never exported.
  float smoothed = -90;
  sound_average_t average = {0};
  unsigned blocks = 0;
  unsigned flat_blocks = 0;
  bool window_has_signal = false;
  unsigned spectrum_frames = 0;
  int64_t report_start = esp_timer_get_time();
  while (true) {
    int result = esp_codec_dev_read(mic, samples, sizeof(samples));
    if (result != ESP_CODEC_DEV_OK) {
      memset(samples, 0, sizeof(samples));
      average = (sound_average_t){0};
      update_ui(-90, -90, true, false);
      puts("SOUND_METER_MIC_ERROR");
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    bool varying = false;
    for (size_t i = 1; i < 800; ++i) varying |= samples[i] != samples[0];
    flat_blocks = varying ? 0 : flat_blocks < 40 ? flat_blocks + 1 : 40;
    window_has_signal |= varying;
    float current_dbfs = sound_dbfs(samples, 800);
    smoothed = sound_smooth(smoothed, current_dbfs);
    float average_dbfs = sound_average_push(&average, current_dbfs);
    float bands[SPECTRUM_BANDS];
    int64_t fft_start = esp_timer_get_time();
    bool spectrum_ready = spectrum_push(&spectrum, samples, 800, bands);
    uint32_t fft_elapsed = (uint32_t)(esp_timer_get_time() - fft_start);
    fft_us += fft_elapsed;
    if (fft_elapsed > fft_max_us) fft_max_us = fft_elapsed;
    if (spectrum_ready) {
      for (size_t i = 0; i < SPECTRUM_BANDS; ++i) {
        band_levels[i] = sound_smooth(band_levels[i], bands[i]);
      }
    }
    memset(samples, 0, sizeof(samples));
    if (update_ui(smoothed, average_dbfs, false, flat_blocks >= 40) && spectrum_ready) ++spectrum_frames;
    if (++blocks % 100 == 0) {
      puts("SOUND_METER_RUNNING_V7");
      puts(window_has_signal ? "SOUND_METER_SIGNAL_V7" : "SOUND_METER_FLAT_V7");
      if (spectrum_frames >= 95) {
        puts("SPECTRUM_RENDERED_V7");
        puts(esp_timer_get_time() - report_start <= 6500000 ? "SPECTRUM_TIMING_OK_V7" : "SPECTRUM_TIMING_SLOW_V7");
      }
      report_performance(esp_timer_get_time() - report_start);
      spectrum_frames = 0;
      report_start = esp_timer_get_time();
      window_has_signal = false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
