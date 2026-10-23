#pragma once
#include <stdbool.h>
#include "lvgl.h"

extern lv_obj_t *spectrum_menu_button;
void spectrum_native_attach_home(void);
void spectrum_native_bootstrap(void);
bool spectrum_native_active(void);
bool spectrum_native_audio_allowed(void);
bool spectrum_native_services_ready(void);
void spectrum_native_close(void);
bool spectrum_native_long_press(void);
bool spectrum_native_long_release(void);
void factory_services_start(void);
