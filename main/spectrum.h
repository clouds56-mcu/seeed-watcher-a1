#pragma once
#include <stddef.h>
#include <stdint.h>

#define SPECTRUM_SIZE 2048
#define SPECTRUM_BANDS 32
#define SPECTRUM_RATE 16000

typedef struct {
  int16_t ring[SPECTRUM_SIZE];
  float real[SPECTRUM_SIZE];
  float imag[SPECTRUM_SIZE];
  float window[SPECTRUM_SIZE];
  float edges[SPECTRUM_BANDS + 1];
  float window_power;
  size_t position;
  size_t filled;
} spectrum_t;

void spectrum_init(spectrum_t *state);
// Returns 0 until a complete window is available, 1 when band levels are ready.
int spectrum_push(spectrum_t *state, const int16_t *samples, size_t count,
                  float bands[SPECTRUM_BANDS]);
