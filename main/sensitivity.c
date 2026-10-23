#include "sensitivity.h"
#include <math.h>

int sensitivity_change(int current_db, sensitivity_event_t event) {
  if (event == SENSITIVITY_RESET) return SENSITIVITY_DEFAULT_DB;
  if (event == SENSITIVITY_UP && current_db < SENSITIVITY_LIMIT_DB) current_db += SENSITIVITY_STEP_DB;
  if (event == SENSITIVITY_DOWN && current_db > -SENSITIVITY_LIMIT_DB) current_db -= SENSITIVITY_STEP_DB;
  return current_db;
}

float sensitivity_level(float band_dbfs, int gain_db) {
  if (band_dbfs <= -90) return 0;
  return fmaxf(0, fminf(1, (band_dbfs + gain_db + 72) / 72));
}
