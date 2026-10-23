#include <assert.h>
#include <math.h>
#include "sensitivity.h"

int main(void) {
  int gain = SENSITIVITY_DEFAULT_DB;
  for (int i = 0; i < 100; ++i) gain = sensitivity_change(gain, SENSITIVITY_UP);
  assert(gain == 24);
  assert(sensitivity_change(gain, SENSITIVITY_RESET) == 0);
  for (int i = 0; i < 100; ++i) gain = sensitivity_change(gain, SENSITIVITY_DOWN);
  assert(gain == -24);
  assert(sensitivity_change(gain, SENSITIVITY_RESET) == 0);
  assert(sensitivity_change(0, SENSITIVITY_UP) == 3);
  assert(sensitivity_change(0, SENSITIVITY_DOWN) == -3);
  assert(fabsf(sensitivity_level(-36, 0) - 0.5f) < 1e-6f);
  assert(sensitivity_level(-36, 3) > sensitivity_level(-36, 0));
  assert(sensitivity_level(-36, -3) < sensitivity_level(-36, 0));
  assert(sensitivity_level(-90, 24) == 0);
  assert(sensitivity_level(-10, 24) == 1);
  assert(sensitivity_level(-65, -24) == 0);
  return 0;
}
