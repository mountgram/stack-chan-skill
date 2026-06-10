# Firmware Build And Flash

Read this when building, flashing, or monitoring StackChan firmware.

## Preconditions

- `vendor/esp-idf` exists in the `stack-chan-skill` skill repo and tools have been installed with `./vendor/esp-idf/install.sh esp32s3`.
- `vendor/StackChan` exists in the `stack-chan-skill` skill repo.
- `app_remote_agent` is installed in `vendor/StackChan/firmware/main/apps/app_remote_agent/`.
- `apps.h` and `main.cpp` register `AppRemoteAgent`.

## Standard Build

From the `stack-chan-skill` skill root:

```bash
. ./vendor/esp-idf/export.sh
cd vendor/StackChan/firmware
python3 ./fetch_repos.py
idf.py build
```

If this is the first build or the target was changed:

```bash
idf.py set-target esp32s3
```

Do not run `set-target` casually if preserving local `sdkconfig` changes matters; it can reinitialize build configuration.

## Device WebSocket URL

The firmware no longer needs a brain URL at build time. After flashing, configure the brain server with `STACKY_DEVICE_WS_URL=ws://STACKCHAN_HOST:6001/stacky/device`.

## Flash

Inspect or ask for the correct serial port before flashing. macOS ports usually look like `/dev/cu.usbmodem*` or `/dev/cu.usbserial*`; Linux ports usually look like `/dev/ttyUSB*` or `/dev/ttyACM*`.

From `vendor/StackChan/firmware`:

```bash
idf.py -p PORT flash
```

Build, flash, and monitor:

```bash
idf.py -p PORT flash monitor
```

Exit monitor with `Ctrl+]`.

## Flashing Notes

- Prefer the base USB-C port when flashing or serial monitoring.
- If multiple serial ports are connected, pass `-p PORT` explicitly.
- If the device needs download mode, follow current M5Stack/StackChan instructions for the hardware revision.
- Use M5Burner or official recovery paths to restore factory firmware if custom firmware testing leaves the device unusable.
