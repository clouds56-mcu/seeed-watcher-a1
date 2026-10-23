# Native factory menu integration

This project builds Seeed's original factory firmware **1.1.7** with a native
`Sound Spectrum` menu entry. Public upstream sources are pinned in `upstream.json`
and materialized under ignored `.deps/watcher-sdk`; `upstream.patch` contains the
factory integration changes. DSP, formatting and drawing are shared with the
standalone demo. This is a customized factory application, not a binary plugin
loader or a separate bootable demo. An official OTA update can replace this
customized application; keep these sources to rebuild and reinstall it.

## Boot and controls

Every reboot starts offline in the original home menu. `SND` opens the spectrum;
`ONLINE` explicitly initializes the original services for the current boot.
Original task/settings entries remain disabled until then. Starting online
services may reconnect saved Wi-Fi/BLE/cloud accounts and resume saved camera
or task flows. Online state is not persisted by this integration.

The offline path initializes display, encoder, microphone, speaker codec and RGB
only. It does not initialize Wi-Fi, BLE, camera AI, SD, storage or cloud services;
AI/SD/Grove power rails are off. PCM and FFT buffers remain in device RAM.

- Select `SND` and press to enter the 32-bar circular spectrum.
- Rotate to change display sensitivity in 3 dB steps, within ±24 dB.
- Short press resets sensitivity to 0 dB.
- Hold the knob to return to the original menu.
- Central dBFS uses the existing one-second linear-power moving average, updated
  at most four times per second. This is not calibrated acoustic dB SPL.

The app uses the original recorder's stream API with 50 ms blocks, reserves audio
against voice/playback, pauses an active task flow, restores the encoder group on
exit and releases the RGB override. An originally paused flow stays paused.
The experimental `ENABLE_VI_SR` wake-word recorder is unsupported and rejected at
compile time; the original default push-to-talk configuration is supported.

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

The build wrapper fetches the pinned public SDK and verifies the patch before
building. ESP-IDF resolves managed components using `dependencies.lock`. Do not
edit the generated SDK: update the versioned patch deliberately when changing
integration code. `spectrum_native.c` is maintained directly in this directory.

**Use the deployment script, not `idf.py flash` or the full generated flash_args.**
It verifies the device's existing factory partition layout, privately backs up the
application sectors it will replace plus OTA metadata, then writes only OTA slot 0
and its boot selection. It preserves the bootloader, partition table, model,
SPIFFS, factory NVS and other data partitions. Backups may contain private data;
keep `.local/` ignored and never preview or export its contents.

Serial output is locally filtered to exact fixed status codes; no raw logs,
identifiers, audio or pixels are forwarded.

## Device lifecycle check

For a development build only, set `CONFIG_WATCHER_NATIVE_SELF_TEST=y` in
`factory/sdkconfig` and rebuild. Then deploy with `--self-test`:

```sh
python scripts/deploy_factory.py --port /dev/cu.YOUR_ESP32_PORT --self-test
```

The test runs two menu-entry/capture/exit cycles, requires non-flat input and 40
render submissions per cycle, sends sensitivity and reset events through the
LVGL controller, and checks recorder idle, home screen and encoder restoration.
It exercises known offline screens only. It does not enable online services.
Disable the self-test and rebuild before the final deployment.

Original online services are compiled in, but their account/network/camera paths
are intentionally left for the user to activate and validate manually. Automated
checks do not provide independent visual inspection of the physical display.

Verified on the connected Watcher: both firmware projects compile; six host checks
pass; the offline device self-test completes two capture/control/exit cycles.
The final deployed build has the self-test disabled.
