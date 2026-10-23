#include <assert.h>
#include <math.h>
#include "sound_level.h"

int main(void) {
  sound_average_t state = {0};
  // Startup uses only available samples, not an artificially quiet full window.
  assert(fabsf(sound_average_push(&state, -20) + 20) < 0.001f);
  assert(fabsf(sound_average_push(&state, -40) + 22.9671f) < 0.001f);
  for (int i = 0; i < SOUND_AVERAGE_BLOCKS; ++i) sound_average_push(&state, -40);
  assert(state.count == SOUND_AVERAGE_BLOCKS);
  assert(fabsf(sound_average_push(&state, -40) + 40) < 0.001f);
  for (int i = 0; i < SOUND_AVERAGE_BLOCKS; ++i) sound_average_push(&state, -90);
  assert(sound_average_push(&state, -90) == -90);
  state = (sound_average_t){0};
  for (int i = 0; i < 10; ++i) sound_average_push(&state, 0);
  for (int i = 0; i < 10; ++i) sound_average_push(&state, -90);
  assert(fabs(10 * log10(state.sum / state.count) + 3.0103) < 0.001f);
  return 0;
}
