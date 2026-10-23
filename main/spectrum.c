#include "spectrum.h"
#include <math.h>
#include <string.h>

static const float pi = 3.14159265358979323846f;

void spectrum_init(spectrum_t *s) {
  memset(s, 0, sizeof(*s));
  for (size_t i = 0; i < SPECTRUM_SIZE; ++i) {
    s->window[i] = 0.5f - 0.5f * cosf(2 * pi * i / SPECTRUM_SIZE);
    s->window_power += s->window[i] * s->window[i];
  }
  for (size_t i = 0; i <= SPECTRUM_BANDS; ++i) {
    s->edges[i] = 31.25f * powf(256.0f, (float)i / SPECTRUM_BANDS);
  }
}

static void fft(spectrum_t *s) {
  for (size_t i = 1, j = 0; i < SPECTRUM_SIZE; ++i) {
    size_t bit = SPECTRUM_SIZE >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float temp = s->real[i];
      s->real[i] = s->real[j];
      s->real[j] = temp;
    }
  }
  for (size_t length = 2; length <= SPECTRUM_SIZE; length <<= 1) {
    float step_r = cosf(-2 * pi / length), step_i = sinf(-2 * pi / length);
    for (size_t start = 0; start < SPECTRUM_SIZE; start += length) {
      float wr = 1, wi = 0;
      for (size_t j = 0; j < length / 2; ++j) {
        size_t a = start + j, b = a + length / 2;
        float tr = wr * s->real[b] - wi * s->imag[b];
        float ti = wr * s->imag[b] + wi * s->real[b];
        s->real[b] = s->real[a] - tr;
        s->imag[b] = s->imag[a] - ti;
        s->real[a] += tr;
        s->imag[a] += ti;
        float next = wr * step_r - wi * step_i;
        wi = wr * step_i + wi * step_r;
        wr = next;
      }
    }
  }
}

int spectrum_push(spectrum_t *s, const int16_t *samples, size_t count,
                  float bands[SPECTRUM_BANDS]) {
  for (size_t i = 0; i < count; ++i) {
    s->ring[s->position] = samples[i];
    s->position = (s->position + 1) % SPECTRUM_SIZE;
    if (s->filled < SPECTRUM_SIZE) ++s->filled;
  }
  if (s->filled < SPECTRUM_SIZE) return 0;
  float mean = 0;
  for (size_t i = 0; i < SPECTRUM_SIZE; ++i) mean += s->ring[i] / 32768.0f;
  mean /= SPECTRUM_SIZE;
  for (size_t i = 0; i < SPECTRUM_SIZE; ++i) {
    s->real[i] = (s->ring[(s->position + i) % SPECTRUM_SIZE] / 32768.0f - mean) * s->window[i];
    s->imag[i] = 0;
  }
  fft(s);
  float power[SPECTRUM_BANDS] = {0};
  const float resolution = (float)SPECTRUM_RATE / SPECTRUM_SIZE;
  for (size_t band = 0; band < SPECTRUM_BANDS; ++band) {
    // Visit only bins touching this band, rather than testing every bin against
    // all 32 bands. Keep fractional boundary weighting and Nyquist handling.
    int first = (int)fmaxf(1, floorf(s->edges[band] / resolution - 0.5f));
    int last = (int)fminf(SPECTRUM_SIZE / 2, ceilf(s->edges[band + 1] / resolution + 0.5f));
    for (int k = first; k <= last; ++k) {
      float energy = (s->real[k] * s->real[k] + s->imag[k] * s->imag[k]) /
                     (SPECTRUM_SIZE * s->window_power);
      if (k != SPECTRUM_SIZE / 2) energy *= 2;
      float left = (k - 0.5f) * resolution, right = (k + 0.5f) * resolution;
      float width = resolution;
      if (k == SPECTRUM_SIZE / 2) {
        right = SPECTRUM_RATE / 2;
        width *= 0.5f;
      }
      float overlap = fminf(right, s->edges[band + 1]) - fmaxf(left, s->edges[band]);
      if (overlap > 0) power[band] += energy * overlap / width;
    }
  }
  for (size_t band = 0; band < SPECTRUM_BANDS; ++band) {
    bands[band] = fmaxf(-90, fminf(0, 10 * log10f(fmaxf(power[band], 1e-9f))));
  }
  // FFT scratch contains transformed audio; retain only the sliding RAM window.
  memset(s->real, 0, sizeof(s->real));
  memset(s->imag, 0, sizeof(s->imag));
  return 1;
}
