#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "esp_timer.h"
#include "sensecap-watcher.h"
#include "spectrum_native.h"
#include "spectrum_display.h"
#include "sound_level.h"
#include "sensitivity.h"
#include "app_audio_recorder.h"
#include "app_audio_player.h"
#include "app_voice_interaction.h"
#include "app_rgb.h"
#include "view/ui/ui.h"
#include "view/ui_manager/pm.h"
#include "tf.h"

#if CONFIG_ENABLE_VI_SR
#error "Native spectrum currently supports the factory's default push-to-talk recorder only"
#endif

lv_obj_t *spectrum_menu_button;
static TaskHandle_t worker;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool active, capturing, close_requested, services_ready, swallow_release;
static int gain;
static lv_obj_t *screen, *previous_screen, *previous_focus;
static lv_group_t *spectrum_group, *previous_group;
static bool resume_flow, recorder_started;
static bool signal_seen;
static spectrum_t spectrum;
static sound_average_t average;
static float bands[SPECTRUM_BANDS], smoothed;
static unsigned frames;
static bool request_open;

bool spectrum_native_active(void) {
  portENTER_CRITICAL(&state_lock);
  bool value = active;
  portEXIT_CRITICAL(&state_lock);
  return value;
}

bool spectrum_native_audio_allowed(void) {
  portENTER_CRITICAL(&state_lock);
  bool value = !capturing || xTaskGetCurrentTaskHandle() == worker;
  portEXIT_CRITICAL(&state_lock);
  return value;
}

bool spectrum_native_services_ready(void) {
  portENTER_CRITICAL(&state_lock);
  bool ready = services_ready;
  portEXIT_CRITICAL(&state_lock);
  return ready;
}

void spectrum_native_close(void) {
  portENTER_CRITICAL(&state_lock);
  close_requested = true;
  portEXIT_CRITICAL(&state_lock);
}

bool spectrum_native_long_press(void) {
  portENTER_CRITICAL(&state_lock);
  bool consumed = active || swallow_release;
  if (consumed) {
    swallow_release = true;
    close_requested = true;
  }
  portEXIT_CRITICAL(&state_lock);
  return consumed;
}

bool spectrum_native_long_release(void) {
  portENTER_CRITICAL(&state_lock);
  bool consumed = swallow_release;
  swallow_release = false;
  portEXIT_CRITICAL(&state_lock);
  return consumed;
}

static void input_event(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  portENTER_CRITICAL(&state_lock);
  if (code == LV_EVENT_KEY) {
    uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_RIGHT) gain = sensitivity_change(gain, SENSITIVITY_UP);
    if (key == LV_KEY_LEFT) gain = sensitivity_change(gain, SENSITIVITY_DOWN);
  } else if (code == LV_EVENT_SHORT_CLICKED) {
    gain = SENSITIVITY_DEFAULT_DB;
  } else if (code == LV_EVENT_LONG_PRESSED) {
    swallow_release = true;
    close_requested = true;
  }
  portEXIT_CRITICAL(&state_lock);
}

static void menu_event(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_FOCUSED) {
    lv_obj_set_y(ui_maintitle, 0);
    lv_label_set_text(ui_maintitle, "Sound\nSpectrum");
  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED && worker && spectrum_native_services_ready()) {
    portENTER_CRITICAL(&state_lock);
    request_open = true;
    portEXIT_CRITICAL(&state_lock);
    xTaskNotifyGive(worker);
  }
}

static lv_obj_t *menu_item(const char *text) {
  lv_obj_t *container = lv_obj_create(ui_mainlist);
  lv_obj_remove_style_all(container);
  lv_obj_set_size(container, 90, 90);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *button = lv_btn_create(container);
  lv_obj_set_size(button, 80, 80);
  lv_obj_center(button);
  lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x253247), 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x526d20), LV_STATE_FOCUSED);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  lv_obj_add_event_cb(button, menu_event, LV_EVENT_ALL, NULL);
  return button;
}

void spectrum_native_attach_home(void) {
  spectrum_menu_button = menu_item("SND");
}

static bool enter_spectrum(void) {
  portENTER_CRITICAL(&state_lock);
  frames = 0;
  signal_seen = false;
  active = true;
  close_requested = false;
  gain = 0;
  portEXIT_CRITICAL(&state_lock);
  resume_flow = false;
  if (services_ready) {
    bool voice_paused_flow = false;
    if (app_vi_pause_for_spectrum(&voice_paused_flow) != ESP_OK) return false;
    int status;
    if (tf_engine_status_get(&status) != ESP_OK) return false;
    resume_flow = voice_paused_flow && status == TF_STATUS_PAUSE;
    bool running_flow = status == TF_STATUS_RUNNING || status == TF_STATUS_STARTING;
    if (running_flow) {
      if (tf_engine_pause_block(pdMS_TO_TICKS(2000)) != ESP_OK) return false;
      tf_engine_status_get(&status);
      if (status != TF_STATUS_PAUSE && status != TF_STATUS_IDLE && status != TF_STATUS_STOP) return false;
      resume_flow = status == TF_STATUS_PAUSE;
    }
  }
  if (app_audio_recorder_status_get() != AUDIO_RECORDER_STATUS_IDLE ||
      app_audio_player_status_get() != AUDIO_PLAYER_STATUS_IDLE) return false;
  portENTER_CRITICAL(&state_lock);
  capturing = true;
  portEXIT_CRITICAL(&state_lock);
  if (app_audio_recorder_stream_start() != ESP_OK) return false;
  recorder_started = true;
  app_rgb_native_override(true);
  spectrum_init(&spectrum);
  average = (sound_average_t){0};
  smoothed = -90;
  signal_seen = false;
  for (size_t i = 0; i < SPECTRUM_BANDS; ++i) bands[i] = -90;
  portENTER_CRITICAL(&state_lock);
  frames = 0;
  portEXIT_CRITICAL(&state_lock);
  lvgl_port_lock(0);
  previous_screen = lv_scr_act();
  previous_group = cur_drv->group;
  previous_focus = previous_group ? lv_group_get_focused(previous_group) : NULL;
  screen = lv_obj_create(NULL);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  spectrum_display_create(screen, "HOLD: BACK");
  lv_obj_t *control = lv_btn_create(screen);
  lv_obj_remove_style_all(control);
  lv_obj_set_size(control, 1, 1);
  lv_obj_add_event_cb(control, input_event, LV_EVENT_ALL, NULL);
  spectrum_group = lv_group_create();
  lv_group_add_obj(spectrum_group, control);
  lv_group_set_editing(spectrum_group, true);
  lv_indev_set_group(cur_drv, spectrum_group);
  lv_indev_reset(cur_drv, NULL);
  lv_indev_wait_release(cur_drv);
  lv_scr_load(screen);
  lvgl_port_unlock();
  puts("NATIVE_SPECTRUM_ENTERED_V1");
  return true;
}

static void leave_spectrum(void) {
  if (recorder_started) {
    app_audio_recorder_stream_stop();
    recorder_started = false;
    bsp_codec_dev_resume();
    bsp_codec_mute_set(false);
  }
  lvgl_port_lock(0);
  if (screen) {
    lv_scr_load(previous_screen);
    lv_indev_set_group(cur_drv, previous_group);
    lv_indev_reset(cur_drv, NULL);
    lv_indev_wait_release(cur_drv);
    if (previous_focus && lv_obj_is_valid(previous_focus)) lv_group_focus_obj(previous_focus);
    lv_group_del(spectrum_group);
    lv_obj_del(screen);
    screen = NULL;
  }
  lvgl_port_unlock();
  portENTER_CRITICAL(&state_lock);
  capturing = active = false;
  portEXIT_CRITICAL(&state_lock);
  memset(&spectrum, 0, sizeof(spectrum));
  app_rgb_native_override(false);
  if (resume_flow) tf_engine_resume();
  resume_flow = false;
  puts("NATIVE_SPECTRUM_EXITED_V1");
}

static void consume_block(int16_t samples[800]) {
  float raw = sound_dbfs(samples, 800);
  portENTER_CRITICAL(&state_lock);
  if (raw > -90) signal_seen = true;
  portEXIT_CRITICAL(&state_lock);
  smoothed = sound_smooth(smoothed, raw);
  float mean = sound_average_push(&average, raw);
  float next[SPECTRUM_BANDS];
  if (spectrum_push(&spectrum, samples, 800, next)) {
    for (size_t i = 0; i < SPECTRUM_BANDS; ++i) bands[i] = sound_smooth(bands[i], next[i]);
  }
  memset(samples, 0, 1600);
  portENTER_CRITICAL(&state_lock);
  int current_gain = gain;
  portEXIT_CRITICAL(&state_lock);
  uint32_t color = spectrum_display_color(smoothed, false, raw <= -90);
  bsp_rgb_set((color >> 16 & 255) / 8, (color >> 8 & 255) / 8, (color & 255) / 8);
  if (lvgl_port_lock(100)) {
    lv_disp_trig_activity(NULL);
    spectrum_display_update(smoothed, mean, bands, current_gain, false, raw <= -90, esp_timer_get_time());
    lvgl_port_unlock();
    portENTER_CRITICAL(&state_lock);
    unsigned count = ++frames;
    portEXIT_CRITICAL(&state_lock);
    if (count == 40) puts("NATIVE_SPECTRUM_RUNNING_V1");
  }
}

static void native_task(void *unused) {
  int16_t samples[800];
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    portENTER_CRITICAL(&state_lock);
    bool open = request_open;
    request_open = false;
    portEXIT_CRITICAL(&state_lock);
    if (!open) continue;
    if (!enter_spectrum()) {
      leave_spectrum();
      lvgl_port_lock(0);
      lv_label_set_text(ui_maintitle, "Audio\nbusy");
      lvgl_port_unlock();
      puts("NATIVE_SPECTRUM_BUSY_V1");
      continue;
    }
    size_t filled = 0;
    int64_t last_audio = esp_timer_get_time();
    while (true) {
      portENTER_CRITICAL(&state_lock);
      bool stop = close_requested;
      portEXIT_CRITICAL(&state_lock);
      if (stop) break;
      size_t bytes = 0;
      uint8_t *chunk = app_audio_recorder_stream_recv(&bytes, pdMS_TO_TICKS(100));
      if (!chunk) {
        if (esp_timer_get_time() - last_audio > 2000000) {
          puts("NATIVE_SPECTRUM_AUDIO_ERROR_V1");
          break;
        }
        continue;
      }
      last_audio = esp_timer_get_time();
      for (size_t offset = 0; offset + 1 < bytes; offset += 2) {
        memcpy(&samples[filled++], chunk + offset, 2);
        if (filled == 800) {
          consume_block(samples);
          filled = 0;
        }
      }
      memset(chunk, 0, bytes);
      app_audio_recorder_stream_free(chunk);
    }
    memset(samples, 0, sizeof(samples));
    leave_spectrum();
  }
}

#if CONFIG_WATCHER_NATIVE_SELF_TEST
static void lifecycle_test(void *unused) {
  for (int round = 0; round < 2; ++round) {
    assert(spectrum_native_services_ready());
    lvgl_port_lock(0);
    assert(lv_group_get_obj_count(g_main) == 5);
    assert(lv_scr_act() == ui_Page_Home);
    lv_event_send(spectrum_menu_button, LV_EVENT_CLICKED, NULL);
    lvgl_port_unlock();
    bool running = false;
    for (int i = 0; i < 150; ++i) {
      portENTER_CRITICAL(&state_lock);
      running = active && frames >= 40;
      portEXIT_CRITICAL(&state_lock);
      if (running) break;
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    assert(running);
    portENTER_CRITICAL(&state_lock);
    bool received_signal = signal_seen;
    portEXIT_CRITICAL(&state_lock);
    assert(received_signal);
    lvgl_port_lock(0);
    lv_group_send_data(spectrum_group, LV_KEY_RIGHT);
    portENTER_CRITICAL(&state_lock);
    int changed_gain = gain;
    portEXIT_CRITICAL(&state_lock);
    assert(changed_gain == 3);
    lv_obj_t *control = lv_group_get_focused(spectrum_group);
    lv_event_send(control, LV_EVENT_SHORT_CLICKED, NULL);
    portENTER_CRITICAL(&state_lock);
    int reset_gain = gain;
    portEXIT_CRITICAL(&state_lock);
    assert(reset_gain == 0);
    lvgl_port_unlock();
    assert(spectrum_native_long_press());
    assert(spectrum_native_long_release());
    for (int i = 0; i < 60 && spectrum_native_active(); ++i) vTaskDelay(pdMS_TO_TICKS(50));
    assert(!spectrum_native_active());
    assert(app_audio_recorder_status_get() == AUDIO_RECORDER_STATUS_IDLE);
    lvgl_port_lock(0);
    assert(lv_scr_act() == ui_Page_Home);
    assert(cur_drv->group == g_main);
    lvgl_port_unlock();
  }
  puts("NATIVE_LIFECYCLE_SELF_TEST_PASS_V1");
  vTaskDelete(NULL);
}
#endif

void spectrum_native_bootstrap(void) {
  assert(xTaskCreatePinnedToCore(native_task, "native_spectrum", 8192, NULL, 3, &worker, 0) == pdPASS);
  factory_services_start();
  portENTER_CRITICAL(&state_lock);
  services_ready = true;
  portEXIT_CRITICAL(&state_lock);
  lvgl_port_lock(0);
  extern GroupInfo group_page_main;
  lv_pm_open_page(g_main, &group_page_main, PM_ADD_OBJS_TO_GROUP,
                 &ui_Page_Home, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Home_screen_init);
  assert(lv_group_get_obj_count(g_main) == 5);
  lv_obj_t *stock[] = {ui_mainbtn1, ui_mainbtn2, ui_mainbtn3, ui_mainbtn4};
  for (size_t i = 0; i < 4; ++i) assert(!lv_obj_has_state(stock[i], LV_STATE_DISABLED));
  lv_group_focus_obj(ui_mainbtn1);
  lvgl_port_unlock();
  puts("\nNATIVE_FACTORY_MENU_READY_V2");
#if CONFIG_WATCHER_NATIVE_SELF_TEST
  assert(xTaskCreate(lifecycle_test, "native_test", 4096, NULL, 2, NULL) == pdPASS);
#endif
}
