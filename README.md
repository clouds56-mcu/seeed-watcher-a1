# Watcher offline circular spectrum

SenseCAP Watcher W1-A: 32 radial frequency bars around a central overall dBFS
reading. Frequencies increase clockwise from the top, from 31.25 Hz to 8 kHz.
Each bar measures total power in its logarithmic band (not power per Hz), with
an independent -72–0 dBFS scale. Central dBFS uses DC-corrected full-band RMS.
RGB reflects overall sound level: amber at -33 dBFS and red at -15 dBFS.
These are relative digital levels, not calibrated dB SPL.

The 16 kHz microphone feeds a 2048-sample sliding FFT with an 800-sample hop
(50 ms, target 20 updates/sec). Hann window power normalization preserves RMS
energy. Fractional bin overlap prevents empty low bands; the lowest bands share
7.8125 Hz FFT resolution, so they are not independent precision measurements.
Bars use fast attack and slow release, a cyan-to-purple gradient and dim tracks.

Purple RGB indicates a microphone error or exactly flat input lasting two seconds
(`NO SIGNAL`). The microphone explicitly selects right I²S slot 1, matching the
official BSP. PCM and FFT data stay in reused device RAM. No Wi-Fi/BLE, camera,
SD or cloud services are initialized, and no audio or spectrum values are logged.

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
the application and OTA selection, and checks fixed startup/heartbeat, non-flat signal, spectrum UI update and
processing-rate codes (at least 95 spectrum updates per 100 hops in 6.5 seconds).
These checks confirm the processing/UI path, not an independent visual inspection.
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

Spectrum host checks (silence, DC, tones across the range, power normalization,
6 dB amplitude change, overlapping windows and Nyquist):

```sh
cc -std=c11 -Wall -Wextra -Werror -Imain main/spectrum.c tests/spectrum_test.c -lm -o /tmp/watcher-spectrum-test
/tmp/watcher-spectrum-test
```
