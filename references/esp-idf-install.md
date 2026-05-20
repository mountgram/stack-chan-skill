# ESP-IDF Install

Read this when installing or activating `idf.py` for this StackChan skill repo.

## Project-Local Convention

Install ESP-IDF under this skill repo's `vendor/` directory:

```text
vendor/esp-idf
```

Keep `vendor/.gitkeep` as an empty placeholder. Do not commit the ESP-IDF checkout.

Use ESP-IDF v5.5.4 for the upstream StackChan firmware unless upstream docs change.

## Install On macOS Or Linux

Run from the `stack-chan-skill` skill root:

```bash
mkdir -p vendor
git clone -b v5.5.4 --recursive https://github.com/espressif/esp-idf.git vendor/esp-idf
./vendor/esp-idf/install.sh esp32s3
```

If the clone already exists, do not overwrite it. Inspect its branch/version before changing it.

## Activate `idf.py`

Source ESP-IDF in every shell that will run `idf.py`:

```bash
. ./vendor/esp-idf/export.sh
```

The leading `.` and space are required. This modifies the current shell environment so `idf.py` is available on `PATH`.

Verify:

```bash
idf.py --version
```

## macOS Prerequisites

If tools are missing, install the common prerequisites first:

```bash
brew install cmake ninja dfu-util ccache
```

If Xcode command line tools are missing:

```bash
xcode-select --install
```

## Linux Prerequisites

For Ubuntu/Debian:

```bash
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
```

## Constraints

- Do not use a user-global `~/esp/esp-idf` path for a blank starter unless the user explicitly asks for it.
- Do not run `idf.py` from `vendor/esp-idf`; source its `export.sh`, then run `idf.py` from the firmware project root.
- For StackChan firmware, the project root is `vendor/StackChan/firmware` inside this skill repo.
