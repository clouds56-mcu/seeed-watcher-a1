#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "esp_timer.h"
#include "spectrum.h"
#include "esp_log.h"
#include "sensecap-watcher.h"
#include "sound_level.h"
#include "controls.h"
#include "sensitivity.h"

static lv_obj_t *value_label, *status_label, *sensitivity_label;
static lv_obj_t *bars[SPECTRUM_BANDS];
static lv_point_t points[SPECTRUM_BANDS][2];
static float direction_x[SPECTRUM_BANDS], direction_y[SPECTRUM_BANDS];
static float band_levels[SPECTRUM_BANDS];
static spectrum_t spectrum;

static lv_obj_t *label(const char *text, int y, const lv_font_t *font) {
  lv_obj_t *obj = lv_label_create(lv_scr_act());
  lv_label_set_text(obj, text);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(0xe8edf5), 0);
  lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, y);
  return obj;
}

static void radial_points(lv_point_t *p, size_t band, float radius) {
  p[0] = (lv_point_t){lroundf(206 + 122 * direction_x[band]), lroundf(206 + 122 * direction_y[band])};
  p[1] = (lv_point_t){lroundf(206 + radius * direction_x[band]), lroundf(206 + radius * direction_y[band])};
}

static void create_ui(void) {
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x101827), 0);
  // Static tracks and persistent point arrays keep all 32 bars visible at silence.
  static lv_point_t tracks[SPECTRUM_BANDS][2];
  for (size_t i = 0; i < SPECTRUM_BANDS; ++i) {
    float angle = -1.57079632679f + 6.28318530718f * i / SPECTRUM_BANDS;
    direction_x[i] = cosf(angle);
    direction_y[i] = sinf(angle);
    radial_points(tracks[i], i, 190);
    lv_obj_t *track = lv_line_create(lv_scr_act());
    lv_line_set_points(track, tracks[i], 2);
    lv_obj_set_style_line_color(track, lv_color_hex(0x253247), 0);
    lv_obj_set_style_line_width(track, 8, 0);
    lv_obj_set_style_line_rounded(track, true, 0);
    radial_points(points[i], i, 125);
    bars[i] = lv_line_create(lv_scr_act());
    lv_line_set_points(bars[i], points[i], 2);
    lv_obj_set_style_line_color(bars[i], lv_color_hsv_to_rgb(155 + i * 160 / 31, 75, 95), 0);
    lv_obj_set_style_line_width(bars[i], 8, 0);
    lv_obj_set_style_line_rounded(bars[i], true, 0);
    band_levels[i] = -90;
  }
  label("SPECTRUM", 132, &lv_font_montserrat_22);
  value_label = label("--.-", 174, &lv_font_montserrat_32);
  label("dBFS", 216, &lv_font_montserrat_16);
  status_label = label("MIC STARTING", 247, &lv_font_montserrat_16);
  sensitivity_label = label("SENS +0 dB", 279, &lv_font_montserrat_16);
  label("CLICK: RESET", 301, LV_FONT_DEFAULT);
}

static bool update_ui(float dbfs, bool failed, bool no_signal) {
  int percent = failed ? 0 : sound_percent(dbfs);
  uint32_t color = failed ? 0xa855f7 : percent >= 75 ? 0xf04452 : percent >= 45 ? 0xf5b942 : 0x34d399;
  const char *status = failed ? "MIC ERROR" : no_signal ? "NO SIGNAL" : percent >= 75 ? "LOUD" : percent >= 45 ? "MODERATE" : "QUIET";
  if (no_signal) color = 0xa855f7;
  ESP_ERROR_CHECK(bsp_rgb_set((color >> 16 & 255) / 8, (color >> 8 & 255) / 8, (color & 255) / 8));
  if (!lvgl_port_lock(100)) return false;
  int gain_db = controls_gain_db();
  lv_label_set_text_fmt(sensitivity_label, "SENS %+d dB", gain_db);
  for (size_t i = 0; i < SPECTRUM_BANDS; ++i) {
    float level = failed || no_signal ? 0 : sensitivity_level(band_levels[i], gain_db);
    // Invalidate old and new bounds when mutating LVGL's retained point storage.
    lv_obj_invalidate(bars[i]);
    radial_points(points[i], i, 125 + 65 * level);
    lv_line_set_points(bars[i], points[i], 2);
  }
  if (failed) lv_label_set_text(value_label, "--.-");
  else lv_label_set_text_fmt(value_label, "%.1f", (double)dbfs);
  lv_label_set_text(status_label, status);
  lv_obj_set_style_text_color(status_label, lv_color_hex(color), 0);
  lvgl_port_unlock();
  return true;
}

void app_main(void) {
  // No network, storage, camera or speech processing is initialized.
  assert(bsp_io_expander_init());
  ESP_ERROR_CHECK(bsp_exp_io_set_level(BSP_PWR_AI_CHIP | BSP_PWR_SDCARD | BSP_PWR_GROVE, 0));
  ESP_ERROR_CHECK(bsp_rgb_init());
  assert(bsp_lvgl_init());
  assert(lvgl_port_lock(0));
  create_ui();
  lvgl_port_unlock();
  controls_init();
  esp_codec_dev_handle_t mic = bsp_audio_codec_microphone_init();
  // Generic mono defaults to LEFT in esp_codec_dev; the mic is on RIGHT.
  esp_codec_dev_sample_info_t format = {
    .sample_rate = 16000, .channel = 2, .bits_per_sample = 16,
    .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1),
  };
  if (!mic || esp_codec_dev_open(mic, &format) != ESP_CODEC_DEV_OK ||
      esp_codec_dev_set_in_gain(mic, 27.0f) != ESP_CODEC_DEV_OK) {
    update_ui(-90, true, false);
    puts("SOUND_METER_MIC_ERROR");
    return;
  }
  spectrum_init(&spectrum);
  puts("SOUND_METER_READY_V4");
  int16_t samples[800]; // 50 ms, reused immediately; never exported.
  float smoothed = -90;
  unsigned blocks = 0;
  unsigned flat_blocks = 0;
  bool window_has_signal = false;
  unsigned spectrum_frames = 0;
  int64_t report_start = esp_timer_get_time();
  while (true) {
    int result = esp_codec_dev_read(mic, samples, sizeof(samples));
    if (result != ESP_CODEC_DEV_OK) {
      memset(samples, 0, sizeof(samples));
      update_ui(-90, true, false);
      puts("SOUND_METER_MIC_ERROR");
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    bool varying = false;
    for (size_t i = 1; i < 800; ++i) varying |= samples[i] != samples[0];
    flat_blocks = varying ? 0 : flat_blocks < 40 ? flat_blocks + 1 : 40;
    window_has_signal |= varying;
    smoothed = sound_smooth(smoothed, sound_dbfs(samples, 800));
    float bands[SPECTRUM_BANDS];
    bool spectrum_ready = spectrum_push(&spectrum, samples, 800, bands);
    if (spectrum_ready) {
      for (size_t i = 0; i < SPECTRUM_BANDS; ++i) {
        band_levels[i] = sound_smooth(band_levels[i], bands[i]);
      }
    }
    memset(samples, 0, sizeof(samples));
    if (update_ui(smoothed, false, flat_blocks >= 40) && spectrum_ready) ++spectrum_frames;
    if (++blocks % 100 == 0) {
      puts("SOUND_METER_RUNNING_V4");
      puts(window_has_signal ? "SOUND_METER_SIGNAL_V4" : "SOUND_METER_FLAT_V4");
      if (spectrum_frames >= 95) {
        puts("SPECTRUM_RENDERED_V4");
        puts(esp_timer_get_time() - report_start <= 6500000 ? "SPECTRUM_TIMING_OK_V4" : "SPECTRUM_TIMING_SLOW_V4");
      }
      spectrum_frames = 0;
      report_start = esp_timer_get_time();
      window_has_signal = false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
