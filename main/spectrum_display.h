#pragma once
#include "lvgl.h"
#include "spectrum.h"

// Caller owns the screen lifetime and holds the LVGL lock for these calls.
void spectrum_display_create(lv_obj_t *root, const char *footer);
uint32_t spectrum_display_color(float dbfs, bool failed, bool no_signal);
void spectrum_display_update(float dbfs, float average_dbfs,
                             const float bands[SPECTRUM_BANDS], int gain_db,
                             bool failed, bool no_signal, int64_t now);
