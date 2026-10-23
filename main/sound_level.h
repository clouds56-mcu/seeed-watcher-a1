#pragma once
#include <stddef.h>
#include <stdint.h>

// AC RMS removes DC offset. Output is clamped to [-90, 0] dBFS.
float sound_dbfs(const int16_t *samples, size_t count);
float sound_smooth(float previous, float current);
int sound_percent(float dbfs);
