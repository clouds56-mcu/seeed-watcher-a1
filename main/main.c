#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "sensecap-watcher.h"
#include "sound_level.h"

static lv_obj_t *bar, *value_label, *status_label;

static lv_obj_t *label(const char *text, int y, const lv_font_t *font) {
  lv_obj_t *obj = lv_label_create(lv_scr_act());
  lv_label_set_text(obj, text);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(0xe8edf5), 0);
  lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, y);
  return obj;
}

static void create_ui(void) {
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x101827), 0);
  label("OFFLINE SOUND", 65, &lv_font_montserrat_22);
  value_label = label("Starting...", 119, &lv_font_montserrat_32);
  bar = lv_bar_create(lv_scr_act());
  lv_obj_set_size(bar, 300, 42);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 185);
  lv_bar_set_range(bar, 0, 100);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x29364b), LV_PART_MAIN);
  status_label = label("MIC STARTING", 249, &lv_font_montserrat_22);
  label("Relative level / dBFS", 295, &lv_font_montserrat_16);
  label("Local processing only", 322, &lv_font_montserrat_16);
}

static void update_ui(float dbfs, bool failed, bool no_signal) {
  int percent = failed ? 0 : sound_percent(dbfs);
  uint32_t color = failed ? 0xa855f7 : percent >= 75 ? 0xf04452 : percent >= 45 ? 0xf5b942 : 0x34d399;
  const char *status = failed ? "MIC ERROR" : no_signal ? "NO SIGNAL" : percent >= 75 ? "LOUD" : percent >= 45 ? "MODERATE" : "QUIET";
  if (no_signal) color = 0xa855f7;
  ESP_ERROR_CHECK(bsp_rgb_set((color >> 16 & 255) / 8, (color >> 8 & 255) / 8, (color & 255) / 8));
  if (lvgl_port_lock(100)) {
    lv_bar_set_value(bar, percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
    if (failed) lv_label_set_text(value_label, "-- dBFS");
    else lv_label_set_text_fmt(value_label, "%.1f dBFS", (double)dbfs);
    lv_label_set_text(status_label, status);
    lv_obj_set_style_text_color(status_label, lv_color_hex(color), 0);
    lvgl_port_unlock();
  }
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
  puts("SOUND_METER_READY_V2");
  int16_t samples[800]; // 50 ms, reused immediately; never exported.
  float smoothed = -90;
  unsigned blocks = 0;
  unsigned flat_blocks = 0;
  bool window_has_signal = false;
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
    memset(samples, 0, sizeof(samples));
    update_ui(smoothed, false, flat_blocks >= 40);
    if (++blocks % 100 == 0) {
      puts("SOUND_METER_RUNNING_V2");
      puts(window_has_signal ? "SOUND_METER_SIGNAL_V2" : "SOUND_METER_FLAT_V2");
      window_has_signal = false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
