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
