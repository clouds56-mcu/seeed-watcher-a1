#pragma once
#include <stdint.h>

// Keep urgent LVGL deadlines urgent; cap only genuinely long idle periods.
static inline uint32_t lvgl_port_task_delay_ms(uint32_t deadline_ms, uint32_t maximum_ms) {
  if (!maximum_ms) maximum_ms = 1;
  if (deadline_ms > maximum_ms) return maximum_ms;
  return deadline_ms ? deadline_ms : 1;
}
