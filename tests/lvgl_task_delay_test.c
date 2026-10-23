#include <assert.h>
#include "lvgl_port_task_delay.h"

int main(void) {
  assert(lvgl_port_task_delay_ms(1, 500) == 1);
  assert(lvgl_port_task_delay_ms(0, 500) == 1);
  assert(lvgl_port_task_delay_ms(30, 500) == 30);
  assert(lvgl_port_task_delay_ms(UINT32_MAX, 500) == 500);
  assert(lvgl_port_task_delay_ms(1, 0) == 1);
  return 0;
}
