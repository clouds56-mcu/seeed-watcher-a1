#include <assert.h>
#include <math.h>
#include "spectrum.h"

static spectrum_t state;
static int16_t samples[SPECTRUM_SIZE];
static float bands[SPECTRUM_BANDS];

static void tone(float hz, float amplitude) {
  for (size_t i = 0; i < SPECTRUM_SIZE; ++i) {
    samples[i] = (int16_t)(amplitude * sinf(6.28318530718f * hz * i / SPECTRUM_RATE));
  }
}

int main(void) {
  spectrum_init(&state);
  assert(!spectrum_push(&state, samples, 800, bands));
  assert(spectrum_push(&state, samples, SPECTRUM_SIZE, bands));
  for (int i = 0; i < SPECTRUM_BANDS; ++i) assert(bands[i] == -90);
  for (size_t i = 0; i < SPECTRUM_SIZE; ++i) samples[i] = 1234;
  spectrum_push(&state, samples, SPECTRUM_SIZE, bands);
  for (int i = 0; i < SPECTRUM_BANDS; ++i) assert(bands[i] == -90);
  const float frequencies[] = {93.75f, 375, 1500, 6000};
  for (size_t test = 0; test < 4; ++test) {
    tone(frequencies[test], 16384);
    spectrum_push(&state, samples, SPECTRUM_SIZE, bands);
    float total_power = 0;
    int peak = 0;
    for (int i = 0; i < SPECTRUM_BANDS; ++i) {
      if (bands[i] > bands[peak]) peak = i;
      total_power += powf(10, bands[i] / 10);
    }
    assert(state.edges[peak] <= frequencies[test] && frequencies[test] <= state.edges[peak + 1]);
    assert(fabsf(10 * log10f(total_power) + 9.031f) < 0.03f);
    float reference = bands[peak];
    tone(frequencies[test], 8192);
    spectrum_push(&state, samples, SPECTRUM_SIZE, bands);
    assert(fabsf(reference - bands[peak] - 6.0206f) < 0.03f);
  }
  // Repeated overlapping chunks exercise circular-buffer wrap and stable mapping.
  tone(1000, 16384);
  for (int i = 0; i < 8; ++i) spectrum_push(&state, samples, 800, bands);
  assert(bands[20] > -14 && bands[20] < -9);
  // Nyquist has no negative-frequency partner and its bin ends at 8 kHz.
  for (size_t i = 0; i < SPECTRUM_SIZE; ++i) samples[i] = i % 2 ? -16384 : 16384;
  spectrum_push(&state, samples, SPECTRUM_SIZE, bands);
  assert(fabsf(bands[31] + 6.0206f) < 0.03f);
  return 0;
}
