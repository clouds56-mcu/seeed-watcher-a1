#include "ui_format.h"
#include "lvgl/src/misc/lv_printf.h"

void ui_format_dbfs(char *buffer, size_t size, int tenths) {
  if (tenths < -900) tenths = -900;
  if (tenths > 0) tenths = 0;
  int magnitude = -tenths;
  lv_snprintf(buffer, size, "%s%d.%d", tenths < 0 ? "-" : "",
              magnitude / 10, magnitude % 10);
}
