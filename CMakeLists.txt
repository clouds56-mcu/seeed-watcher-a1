cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/vendor")
set(COMPONENTS main)
# 32 bars plus labels can exceed LVGL's 32-entry dirty-area list.
add_compile_definitions(LV_INV_BUF_SIZE=128)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(watcher_sound_meter)
