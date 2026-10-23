# Native factory menu integration

This project builds Seeed's original factory firmware **1.1.7** with a native
`Sound Spectrum` menu entry. Public upstream sources are pinned in `upstream.json`
and materialized under ignored `.deps/watcher-sdk`; `upstream.patch` contains the
factory integration changes. DSP, formatting and drawing are shared with the
standalone demo. This is a customized factory application, not a binary plugin
loader or a separate bootable demo. An official OTA update can replace this
customized application; keep these sources to rebuild and reinstall it.

## Boot and controls

Original factory services initialize at boot, including networking, cloud, voice,
camera AI, storage and saved task handling, using the existing factory settings.
There is no ONLINE entry or per-boot offline gate. The full original menu remains
enabled: Task Templates, Current Tasks, Extension and Setting, plus SND. Startup
opens this complete menu after service initialization.

The **spectrum App** keeps audio and FFT buffers in device RAM and never uploads
audio or spectrum data. It uses the original recorder stream API with exclusive
ownership, pauses voice/task flow activity while capturing, then releases it on
exit. This local-only promise applies to SND; the original factory services retain
their normal network/reporting/voice/image-upload behavior, as requested.

- Select `SND` and press to enter the 32-bar circular spectrum.
- Rotate to change display sensitivity in 3 dB steps, within ±24 dB.
- Short press resets sensitivity to 0 dB.
- Hold the knob to return to the original menu.
- Central dBFS uses a one-second linear-power moving average, updated at most four
  times per second. This is not calibrated acoustic dB SPL.

The app reserves the original recorder in 50 ms blocks against voice/playback,
restores the encoder group on exit and releases the RGB override. An originally
paused flow stays paused. Factory page-change helpers cannot replace the spectrum
screen while it is active. The experimental `ENABLE_VI_SR` wake-word recorder is
unsupported and rejected at compile time; the original default push-to-talk
configuration is supported.

LVGL objects and drawing scratch allocations use PSRAM. The two 40-row display
DMA buffers and LVGL task stack remain in internal RAM. This keeps internal RAM
available for the original Bluetooth controller when the full factory UI and
services run together. Directory scans and bounded 4 KiB image reads yield so the idle
watchdog can run. A fixed numeric startup report records free/largest internal
heap before Bluetooth initialization; it contains no device or audio data.

## Build and deploy

Use ESP-IDF 5.2.3 and its ESP32-S3 toolchain (upstream targeted 5.2.1):

```sh
scripts/build_factory.sh
python scripts/deploy_factory.py --port /dev/cu.YOUR_ESP32_PORT
```

Or use the development toolchain:

```sh
WATCHER_IDF_PATH=/private/tmp/watcher-esp-idf \
WATCHER_TOOLS_PATH=/private/tmp/watcher-idf-tools scripts/build_factory.sh
```

The wrapper fetches the pinned public SDK and verifies the patch before building.
ESP-IDF resolves managed components using `dependencies.lock`. Do not edit the
generated SDK: update the versioned patch deliberately for integration changes.
`spectrum_native.c` is maintained directly in this directory.

**Use the deployment script, not `idf.py flash` or the generated flash_args.**
It verifies the existing factory partition layout, privately backs up application
sectors it will replace plus OTA metadata, then writes only OTA slot 0 and its boot
selection. It preserves the bootloader, partition table, model, SPIFFS, factory NVS
and other data partitions. Backups may contain private data; keep `.local/` ignored
and never preview or export its contents.

Serial output is locally filtered to exact fixed status codes and numeric transfer
progress; no raw logs, identifiers, audio or pixels are forwarded.

## Device lifecycle check

For a development build only, set `CONFIG_WATCHER_NATIVE_SELF_TEST=y` in
`factory/sdkconfig` and rebuild. Then deploy with `--self-test`:

```sh
python scripts/deploy_factory.py --port /dev/cu.YOUR_ESP32_PORT --self-test
```

The test initializes original services, checks that all five menu entries are
available, and runs two SND capture/control/exit cycles. It requires non-flat input
and 40 render submissions per cycle, sends sensitivity/reset events through the
LVGL controller, and checks recorder idle, home screen and encoder restoration.
It navigates only the known home and spectrum screens. It does not exercise
account/detail pages or certify every original cloud service. Original networking
retains its normal startup behavior during this test. Disable the self-test and
rebuild before final deployment. These checks are not independent visual
inspection of the physical display.

Serial protocol regression checks use generated fixtures only:

```sh
python3 tests/factory_runtime_test.py
```

They cover the colored stock console prompt, ignored private-looking noise,
normal boot observation, repeated boots, watchdog failures and fatal exceptions.

Validated on the connected Watcher for `1.1.7-spectrum.2`: full factory service
initialization and five enabled menu entries; two SND capture/control/exit cycles;
normal production startup with the self-test disabled and no watchdog/crash during
the observation window. The synthetic serial protocol suite passes all six cases.
