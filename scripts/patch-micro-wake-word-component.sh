#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
skill_dir="$(cd -- "$script_dir/.." && pwd)"
component_dir="$skill_dir/vendor/StackChan/firmware/managed_components/micro_wake_word"
helpers_cpp="$component_dir/esphome/core/helpers.cpp"
cmake_file="$component_dir/CMakeLists.txt"

if [[ ! -d "$component_dir" ]]; then
    cat >&2 <<EOF
ERROR: micro_wake_word managed component was not found:
  $component_dir

Run idf.py build once from vendor/StackChan/firmware so ESP-IDF fetches
managed components, then run this script again.
EOF
    exit 1
fi

if [[ -f "$helpers_cpp" ]] && grep -q '#include "esp32/rom/crc.h"' "$helpers_cpp"; then
    python3 - "$helpers_cpp" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
patched = text.replace(
    '#include "esp32/rom/crc.h"',
    '#include "esp_rom_crc.h"\n#define crc16_le esp_rom_crc16_le\n#define crc16_be esp_rom_crc16_be',
)
if patched != text:
    path.write_text(patched)
PY
    echo "checked $helpers_cpp"
fi

if [[ -f "$cmake_file" ]]; then
    python3 - "$cmake_file" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
patched = text
needle = 'REQUIRES\n    "esp_timer"'
replacement = 'REQUIRES\n    "esp_timer"\n    "ESPMicroSpeechFeatures"\n    "espressif__esp-tflite-micro"'
if '"ESPMicroSpeechFeatures"' not in patched and needle in patched:
    patched = patched.replace(needle, replacement, 1)
if '"espressif__esp-tflite-micro"' not in patched and '"ESPMicroSpeechFeatures"' in patched:
    patched = patched.replace('"ESPMicroSpeechFeatures"', '"ESPMicroSpeechFeatures"\n    "espressif__esp-tflite-micro"', 1)
if patched != text:
    path.write_text(patched)
PY
    echo "checked $cmake_file"
fi

echo "micro_wake_word component patches are present"
