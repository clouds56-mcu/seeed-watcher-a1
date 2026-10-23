# Watcher offline sound meter

SenseCAP Watcher W1-A demo: 16 kHz mono microphone input, DC-corrected RMS,
asymmetric attack/release smoothing, screen volume bar and green/amber/red RGB.
Purple indicates a microphone error. No Wi-Fi/BLE, camera, SD or cloud services
are initialized. PCM stays in a reused device RAM buffer and is never logged.

The display reports relative dBFS, not calibrated sound pressure dB SPL. The bar
maps -60 to 0 dBFS onto 0–100%; amber starts at 45%, red at 75%. Microphone gain
is fixed at 27 dB. These thresholds are for demonstration, not noise compliance.

## Build

Use ESP-IDF v5.2.3 with the ESP32-S3 toolchain, activate its `export.sh`, then:

```sh
idf.py build
```

Board drivers and LVGL are vendored from Seeed's
[official firmware SDK](https://github.com/Seeed-Studio/SenseCAP-Watcher-Firmware),
commit `8e37f7ccd3d836bb99f483e0f6e74e8252406644` (Apache-2.0 / LVGL MIT).
Unused LVGL demos, examples, documentation and scripts are omitted.
The unused rlottie dependency is removed from the vendored LVGL manifest.
Managed driver dependencies are resolved by ESP-IDF; keep `dependencies.lock` in Git.
This demo uses the vendor's C drivers directly to keep the firmware integration
small and aligned with their supported SDK.

## Deploy

```sh
python scripts/deploy.py --port /dev/cu.YOUR_ESP32_PORT
```

The script verifies the supported factory partition layout, backs up all sectors
that will be overwritten in OTA slot 0
and boot selection privately under ignored `.local/`, writes and verifies only
the application and OTA selection, and checks fixed demo startup/heartbeat codes.
It preserves the bootloader, partition table, factory configuration and other
data partitions. It replaces the application in OTA slot 0 and selects it on boot.
Backups can contain private data: do not preview, upload, or commit them.
Raw serial output is captured locally and never forwarded. An unsupported
partition layout stops deployment before any writes.

For the toolchain installed during development:

```sh
WATCHER_IDF_PATH=/private/tmp/watcher-esp-idf WATCHER_TOOLS_PATH=/private/tmp/watcher-idf-tools scripts/build.sh
```

The BSP manifest also exposes its public header dependencies, and the touch
initializer is compiled only when touch input is enabled.

## Host check

```sh
cc -std=c11 -Wall -Wextra -Werror -Imain main/sound_level.c tests/sound_level_test.c -lm -o /tmp/watcher-sound-test
/tmp/watcher-sound-test
```
