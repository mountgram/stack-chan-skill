#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$script_dir/.."

cmake_file="$repo_root/vendor/StackChan/firmware/main/CMakeLists.txt"
sdkconfig_defaults="$repo_root/vendor/StackChan/firmware/sdkconfig.defaults"

if [[ ! -f "$cmake_file" ]]; then
    echo "ERROR: CMakeLists.txt not found at $cmake_file" >&2
    exit 1
fi

if grep -q 'esp_http_server' "$cmake_file"; then
    echo "esp_http_server dependency already present in CMakeLists.txt"
else
    sed -i '' '/esp_netif/a\
                        esp_http_server
' "$cmake_file"
    echo "Added esp_http_server dependency to CMakeLists.txt"
fi

if [[ -f "$sdkconfig_defaults" ]] && ! grep -q '^CONFIG_HTTPD_WS_SUPPORT=y$' "$sdkconfig_defaults"; then
    printf '\nCONFIG_HTTPD_WS_SUPPORT=y\n' >> "$sdkconfig_defaults"
    echo "Enabled HTTPD WebSocket support in sdkconfig.defaults"
fi
