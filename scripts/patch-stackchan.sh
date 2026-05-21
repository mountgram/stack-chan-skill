#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$script_dir/.."

cmake_file="$repo_root/vendor/StackChan/firmware/main/CMakeLists.txt"

if [[ ! -f "$cmake_file" ]]; then
    echo "ERROR: CMakeLists.txt not found at $cmake_file" >&2
    exit 1
fi

if grep -q 'STACKY_WS_URL' "$cmake_file"; then
    echo "STACKY_WS_URL forwarding already present in CMakeLists.txt"
    exit 0
fi

# Find the closing ) of target_compile_definitions(${COMPONENT_LIB} ... and add the forwarding after it
marker='PRIVATE BUILTIN_TEXT_FONT=${BUILTIN_TEXT_FONT} BUILTIN_ICON_FONT=${BUILTIN_ICON_FONT}'
insert='                    )\
if(DEFINED ENV{STACKY_WS_URL})\
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "STACKY_WS_URL=\\"$ENV{STACKY_WS_URL}\\"")\
endif()'

sed -i '' "/${marker}/,/^                    )$/{
  /^                    )$/{
    a\\
${insert}
    d
  }
}" "$cmake_file"

echo "Added STACKY_WS_URL forwarding to CMakeLists.txt"
