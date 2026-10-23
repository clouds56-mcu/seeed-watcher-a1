#include <assert.h>
#include <math.h>
#include "sound_level.h"

int main(void) {
  int16_t silence[160] = {0}, dc[160], wave[160];
  for (int i = 0; i < 160; ++i) {
    dc[i] = 1234;
    wave[i] = (int16_t)(16384 * sin(2 * 3.141592653589793 * i / 16));
  }
  assert(sound_dbfs(silence, 160) == -90);
  assert(sound_dbfs(dc, 160) == -90);
  assert(fabsf(sound_dbfs(wave, 160) + 9.031f) < 0.01f);
  assert(sound_percent(-90) == 0 && sound_percent(0) == 100);
  assert(sound_percent(-30) == 50);
  assert(sound_smooth(-60, -20) > -40);
  assert(sound_smooth(-20, -60) > -30);
  return 0;
}
