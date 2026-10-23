#pragma once

#include <stddef.h>
#include "esp_heap_caps.h"

// UI objects share external RAM; retain internal RAM for DMA and radio buffers.
static inline void *watcher_lv_alloc(size_t size) {
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static inline void *watcher_lv_realloc(void *memory, size_t size) {
  return heap_caps_realloc(memory, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
