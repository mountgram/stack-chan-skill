# Firmware Build And Flash

Read this when building, flashing, or monitoring StackChan firmware.

## Preconditions

- `vendor/esp-idf` exists in the `stack-chan-skill` skill repo and tools have been installed with `./vendor/esp-idf/install.sh esp32s3`.
- `vendor/StackChan` exists in the `stack-chan-skill` skill repo.
- `app_remote_agent` is installed in `vendor/StackChan/firmware/main/apps/app_remote_agent/`.
- `apps.h` and `main.cpp` register `AppRemoteAgent`.

## Standard Build

Source the main repo's `.env` (which provides `STACKY_PUBLIC_BASE_URL` and `STACKY_DEVICE_TOKEN`). `STACKY_WS_URL` will be auto-constructed from those if not already set. Then from the `stack-chan-skill` skill root:

```bash
set -a && . ../../../.env && set +a
if [ -z "$STACKY_WS_URL" ] && [ -n "$STACKY_PUBLIC_BASE_URL" ] && [ -n "$STACKY_DEVICE_TOKEN" ]; then
    WS_BASE=$(echo "$STACKY_PUBLIC_BASE_URL" | sed 's|^http|ws|')
    export STACKY_WS_URL="${WS_BASE}/stacky/device?token=${STACKY_DEVICE_TOKEN}"
fi
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

## Build With WebSocket URL

Prefer sourcing the main repo `.env` (see Standard Build above) which should export `STACKY_WS_URL`. To override inline from `vendor/StackChan/firmware` after sourcing ESP-IDF:

```bash
STACKY_WS_URL='ws://LAN_HOST:6001/stacky/device?token=TOKEN' idf.py build
```

Use a LAN IP or hostname StackChan can reach. `localhost` points at StackChan itself, not the development machine.

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
