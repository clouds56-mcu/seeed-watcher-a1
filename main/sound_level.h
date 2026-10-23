#pragma once
#include <stddef.h>
#include <stdint.h>

// AC RMS removes DC offset. Output is clamped to [-90, 0] dBFS.
float sound_dbfs(const int16_t *samples, size_t count);
float sound_smooth(float previous, float current);
int sound_percent(float dbfs);

#define SOUND_AVERAGE_BLOCKS 20 // Twenty 50-ms blocks: one second of audio.
typedef struct {
  double power[SOUND_AVERAGE_BLOCKS];
  double sum;
  size_t position;
  size_t count;
} sound_average_t;

// Average linear power before converting to dBFS; do not average logarithms.
float sound_average_push(sound_average_t *state, float dbfs);
