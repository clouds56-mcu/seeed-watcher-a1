cmake_minimum_required(VERSION 3.16)
get_filename_component(WATCHER_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(WATCHER_SDK "${WATCHER_ROOT}/.deps/watcher-sdk")
set(EXTRA_COMPONENT_DIRS
  "${WATCHER_ROOT}/vendor"
  "${WATCHER_SDK}/components/esp_jpeg_simd"
  "${WATCHER_SDK}/examples/factory_firmware/main"
)
# Preserve the upstream factory project's legacy logging compile options.
add_compile_options(-Wno-error=format= -Wno-format)
add_compile_definitions(
  LV_INV_BUF_SIZE=128
  LV_MEM_CUSTOM_INCLUDE="${WATCHER_ROOT}/factory/lvgl_heap.h"
  LV_MEM_CUSTOM_ALLOC=watcher_lv_alloc
  LV_MEM_CUSTOM_REALLOC=watcher_lv_realloc
  LV_MEM_CUSTOM_FREE=heap_caps_free
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(factory_spectrum)
