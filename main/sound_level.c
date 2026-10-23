#include "sound_level.h"
#include <math.h>

float sound_dbfs(const int16_t *samples, size_t count) {
  if (!samples || !count) return -90.0f;
  double sum = 0, squares = 0;
  for (size_t i = 0; i < count; ++i) {
    double value = samples[i];
    sum += value;
    squares += value * value;
  }
  double mean = sum / count;
  double power = fmax(0, squares / count - mean * mean);
  if (power <= 0) return -90.0f;
  return (float)fmax(-90, fmin(0, 10 * log10(power / (32768.0 * 32768.0))));
}

float sound_smooth(float previous, float current) {
  float alpha = current > previous ? 0.65f : 0.12f;
  return previous + alpha * (current - previous);
}

int sound_percent(float dbfs) {
  return (int)lroundf(fmaxf(0, fminf(100, (dbfs + 60) * (100.0f / 60))));
}

float sound_average_push(sound_average_t *state, float dbfs) {
  double power = dbfs <= -90 ? 0 : pow(10, fmin(0, dbfs) / 10);
  state->sum -= state->power[state->position];
  state->power[state->position] = power;
  state->sum += power;
  state->position = (state->position + 1) % SOUND_AVERAGE_BLOCKS;
  if (state->count < SOUND_AVERAGE_BLOCKS) ++state->count;
  double mean = state->sum / state->count;
  return (float)fmax(-90, fmin(0, 10 * log10(fmax(mean, 1e-9))));
}
