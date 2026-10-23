# Watcher offline circular spectrum

The integrated factory-menu version is in [factory/README.md](factory/README.md).
It initializes original factory services normally, preserves all four original
menu entries, and adds `SND`. The spectrum App processes audio locally without
uploads. The instructions below describe the standalone firmware.

SenseCAP Watcher W1-A: 32 radial frequency bars around a central overall dBFS
reading. Frequencies increase clockwise from the top, from 31.25 Hz to 8 kHz.
Each bar measures total power in its logarithmic band (not power per Hz), with
an independent -72–0 dBFS scale. Central dBFS uses a one-second moving average of DC-corrected full-band RMS power
(20 blocks of 50 ms), published at most every 250 ms. Average linear power before
converting to dBFS; do not average dB numbers. Startup uses available blocks, and
read failures clear history. Spectrum bars and RGB retain their fast response.
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

## Knob sensitivity

Turn clockwise to raise spectrum display sensitivity, counterclockwise to lower
it. Each detent changes display gain by 3 dB, limited to -24 through +24 dB.
`SENS +0 dB` is the default. A single short click restores it. Settings are RAM-only
and return to default on reboot. Overall dBFS, RGB thresholds, microphone gain
and FFT measurements do not change. Digital silence stays collapsed at all settings.
The physical encoder and debounced button use their own callbacks; LVGL encoder
input remains disabled to prevent duplicate ownership. Input callbacks update
only a small synchronized gain value, and the audio/UI task renders it.

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
processing-rate and control-driver initialization codes (at least 95 spectrum updates per 100 hops in 6.5 seconds).
The script also projects two aggregate performance reports: elapsed milliseconds,
completed LVGL refreshes, average/maximum FFT and UI-lock wall times, average/maximum
LVGL render milliseconds and average rendered pixel count. No pixels or audio values
are exported. Submitted UI updates are separate from completed refreshes; use
`frames / elapsed_seconds` to assess actual rendering. These checks are not an
independent visual inspection.
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

Sensitivity mapping checks:

```sh
cc -std=c11 -Wall -Wextra -Werror -Imain main/sensitivity.c tests/sensitivity_test.c -lm -o /tmp/watcher-sensitivity-test
/tmp/watcher-sensitivity-test
```

## Rendering performance

Each radial line uses a fixed local bounding box rather than a canvas-sized box.
Only changed integer bar lengths, labels and RGB colors are updated. The global
`LV_INV_BUF_SIZE=128` build definition keeps all LVGL users on the same display
structure layout and prevents 32 bars plus labels overflowing the dirty-area list.
Two 40-row internal DMA buffers replace the full-screen PSRAM draw buffers.
Performance compiler optimization is enabled. LVGL is pinned to core 1; the audio
and spectrum loop stays on the ESP-IDF main task on core 0.

The LVGL port preserves urgent timer deadlines and always yields at least one
FreeRTOS tick. Its maximum idle sleep is 10 ms: this port has no invalidation wakeup,
so a 500 ms idle delay otherwise stalls newly submitted frames after LVGL pauses
its refresh timer. The spectrum integrator visits only bins overlapping each band.
The fractional-bin weighting and RMS normalization are unchanged.

Timer regression check:

```sh
cc -std=c11 -Wall -Wextra -Werror -Ivendor/esp_lvgl_port/include tests/lvgl_task_delay_test.c -o /tmp/watcher-delay-test
/tmp/watcher-delay-test
```

Two approximately five-second device windows during this change measured:

| Metric | Original | Optimized, split cores |
| --- | --- | --- |
| Completed LVGL refresh rate | ~1.7 FPS | ~17 FPS |
| Spectrum processing average wall time | ~32 ms | ~4.9 ms |
| LVGL rendering average time | ~85 ms | ~50 ms |

These are local runtime measurements under the observed scene, not a guaranteed
frame rate. The 20 FPS target is not fully reached; rendering is the remaining
bottleneck. Wall-time measurements include task scheduling and contention.

## Numeric display

Overall dBFS is formatted as integer tenths using the actual LVGL formatter, so
it does not require `LV_SPRINTF_USE_FLOAT`. Negative fractional values such as
`-0.1` retain their sign. Boot checks verify representative strings and emit only
a fixed success code; deployment requires that code.

```sh
cc -std=c11 -Wall -Wextra -Werror -DLV_CONF_SKIP -Imain -Ivendor main/ui_format.c vendor/lvgl/src/misc/lv_printf.c tests/ui_format_test.c -o /tmp/watcher-format-test
/tmp/watcher-format-test
```

Moving-average checks (startup, linear-power averaging, window eviction, silence
and a half-power mixture):

```sh
cc -std=c11 -Wall -Wextra -Werror -Imain main/sound_level.c tests/sound_average_test.c -lm -o /tmp/watcher-average-test
/tmp/watcher-average-test
```
