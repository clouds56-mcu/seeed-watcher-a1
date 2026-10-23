#pragma once

#define SENSITIVITY_DEFAULT_DB 0
#define SENSITIVITY_LIMIT_DB 24
#define SENSITIVITY_STEP_DB 3

typedef enum {
  SENSITIVITY_DOWN,
  SENSITIVITY_UP,
  SENSITIVITY_RESET,
} sensitivity_event_t;

int sensitivity_change(int current_db, sensitivity_event_t event);
float sensitivity_level(float band_dbfs, int gain_db);
