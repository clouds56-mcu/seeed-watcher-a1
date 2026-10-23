#include <assert.h>
#include <string.h>
#include "ui_format.h"
#include "lvgl/src/misc/lv_printf.h"

int main(void) {
  // Exercise the real embedded formatter with float support disabled.
  assert(LV_SPRINTF_USE_FLOAT == 0);
  const int values[] = {-900, -421, -10, -1, 0};
  const char *expected[] = {"-90.0", "-42.1", "-1.0", "-0.1", "0.0"};
  char text[16];
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
    ui_format_dbfs(text, sizeof(text), values[i]);
    assert(strcmp(text, expected[i]) == 0);
  }
  char small[3];
  ui_format_dbfs(small, sizeof(small), -900);
  assert(strcmp(small, "-9") == 0);
  return 0;
}
