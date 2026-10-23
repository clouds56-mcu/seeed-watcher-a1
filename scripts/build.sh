#!/bin/sh
set -eu
# Optional local toolchain paths; a standard exported ESP-IDF environment also works.
if [ -n "${WATCHER_IDF_PATH:-}" ]; then
  export IDF_PATH="$WATCHER_IDF_PATH"
fi
if [ -n "${WATCHER_TOOLS_PATH:-}" ]; then
  export IDF_TOOLS_PATH="$WATCHER_TOOLS_PATH"
  export IDF_PYTHON_ENV_PATH="$IDF_TOOLS_PATH/python_env/idf5.2_py3.12_env"
  export PATH="$IDF_PYTHON_ENV_PATH/bin:$IDF_TOOLS_PATH/tools/xtensa-esp-elf/esp-13.2.0_20230928/xtensa-esp-elf/bin:$PATH"
fi
python "$IDF_PATH/tools/idf.py" build
