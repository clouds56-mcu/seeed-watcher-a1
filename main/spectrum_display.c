#include "spectrum_display.h"
#include "ui_format.h"
#include "sound_level.h"
#include "sensitivity.h"
#include <math.h>
#include <stdlib.h>
#include <limits.h>

// One active spectrum page; all retained points live until the next create call.
static lv_obj_t *display_root;
static int previous_gain, previous_tenths;
static const char *previous_status;
static int64_t last_value_update;
static bool previous_failed;
static lv_obj_t *value_label, *status_label, *sensitivity_label;
static lv_obj_t *bars[SPECTRUM_BANDS];
static lv_point_t points[SPECTRUM_BANDS][2];
static lv_point_t origins[SPECTRUM_BANDS];
static int last_radius[SPECTRUM_BANDS];
static float direction_x[SPECTRUM_BANDS], direction_y[SPECTRUM_BANDS];


uint32_t spectrum_display_color(float dbfs, bool failed, bool no_signal) {
  int percent = sound_percent(dbfs);
  return failed || no_signal ? 0xa855f7 : percent >= 75 ? 0xf04452 : percent >= 45 ? 0xf5b942 : 0x34d399;
}

static lv_obj_t *label(const char *text, int y, const lv_font_t *font) {
  lv_obj_t *obj = lv_label_create(display_root);
  lv_label_set_text(obj, text);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(0xe8edf5), 0);
  lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, y);
  return obj;
}

static void radial_points(lv_point_t *p, size_t band, float radius) {
  p[0] = (lv_point_t){lroundf(206 + 122 * direction_x[band]) - origins[band].x, lroundf(206 + 122 * direction_y[band]) - origins[band].y};
  p[1] = (lv_point_t){lroundf(206 + radius * direction_x[band]) - origins[band].x, lroundf(206 + radius * direction_y[band]) - origins[band].y};
}

void spectrum_display_create(lv_obj_t *root, const char *footer) {
  display_root = root;
  previous_gain = previous_tenths = INT_MIN;
  previous_status = NULL;
  last_value_update = 0;
  previous_failed = false;
  lv_obj_set_style_bg_color(display_root, lv_color_hex(0x101827), 0);
  // Static tracks and persistent point arrays keep all 32 bars visible at silence.
  static lv_point_t tracks[SPECTRUM_BANDS][2];
  for (size_t i = 0; i < SPECTRUM_BANDS; ++i) {
    float angle = -1.57079632679f + 6.28318530718f * i / SPECTRUM_BANDS;
    direction_x[i] = cosf(angle);
    direction_y[i] = sinf(angle);
    int inner_x = lroundf(206 + 122 * direction_x[i]);
    int inner_y = lroundf(206 + 122 * direction_y[i]);
    int outer_x = lroundf(206 + 190 * direction_x[i]);
    int outer_y = lroundf(206 + 190 * direction_y[i]);
    origins[i] = (lv_point_t){inner_x < outer_x ? inner_x : outer_x,
                             inner_y < outer_y ? inner_y : outer_y};
    radial_points(tracks[i], i, 190);
    lv_obj_t *track = lv_line_create(display_root);
    lv_line_set_points(track, tracks[i], 2);
    lv_obj_set_pos(track, origins[i].x, origins[i].y);
    lv_obj_set_size(track, abs(outer_x - inner_x) + 1, abs(outer_y - inner_y) + 1);
    lv_obj_set_style_line_color(track, lv_color_hex(0x253247), 0);
    lv_obj_set_style_line_width(track, 8, 0);
    lv_obj_set_style_line_rounded(track, true, 0);
    radial_points(points[i], i, 125);
    bars[i] = lv_line_create(display_root);
    lv_line_set_points(bars[i], points[i], 2);
    lv_obj_set_pos(bars[i], origins[i].x, origins[i].y);
    lv_obj_set_size(bars[i], abs(outer_x - inner_x) + 1, abs(outer_y - inner_y) + 1);
    last_radius[i] = 125;
    lv_obj_set_style_line_color(bars[i], lv_color_hsv_to_rgb(155 + i * 160 / 31, 75, 95), 0);
    lv_obj_set_style_line_width(bars[i], 8, 0);
    lv_obj_set_style_line_rounded(bars[i], true, 0);
  }
  label("SPECTRUM", 132, &lv_font_montserrat_22);
  value_label = label("--.-", 174, &lv_font_montserrat_32);
  label("dBFS / 1s AVG", 216, &lv_font_montserrat_16);
  status_label = label("MIC STARTING", 247, &lv_font_montserrat_16);
  sensitivity_label = label("SENS +0 dB", 279, &lv_font_montserrat_16);
  label(footer, 301, LV_FONT_DEFAULT);
}

void spectrum_display_update(float dbfs, float average_dbfs,
                             const float band_levels[SPECTRUM_BANDS], int gain_db,
                             bool failed, bool no_signal, int64_t now) {
  int percent = sound_percent(dbfs);
  uint32_t color = spectrum_display_color(dbfs, failed, no_signal);
  const char *status = failed ? "MIC ERROR" : no_signal ? "NO SIGNAL" : percent >= 75 ? "LOUD" : percent >= 45 ? "MODERATE" : "QUIET";
  if (gain_db != previous_gain) {
    lv_label_set_text_fmt(sensitivity_label, "SENS %+d dB", gain_db);
    previous_gain = gain_db;
  }
  for (size_t i = 0; i < SPECTRUM_BANDS; ++i) {
    float level = failed || no_signal ? 0 : sensitivity_level(band_levels[i], gain_db);
    int radius = lroundf(125 + 65 * level);
    if (radius != last_radius[i]) {
      radial_points(points[i], i, radius);
      lv_line_set_points(bars[i], points[i], 2);
      last_radius[i] = radius;
    }
  }
  if (failed || failed != previous_failed || previous_tenths == INT_MIN || now - last_value_update >= 250000) {
    int tenths = failed ? INT_MIN + 1 : lroundf(average_dbfs * 10);
    if (tenths != previous_tenths) {
      if (failed) lv_label_set_text(value_label, "--.-");
      else {
        char text[16];
        ui_format_dbfs(text, sizeof(text), tenths);
        lv_label_set_text(value_label, text);
      }
      previous_tenths = tenths;
    }
    last_value_update = now;
    previous_failed = failed;
  }
  if (status != previous_status) {
    lv_label_set_text(status_label, status);
    lv_obj_set_style_text_color(status_label, lv_color_hex(color), 0);
    previous_status = status;
  }
}
