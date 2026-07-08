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

if grep -q 'nvs_flash' "$cmake_file"; then
    echo "nvs_flash dependency already present in CMakeLists.txt"
else
    sed -i '' '/esp_http_server/a\
                        nvs_flash
' "$cmake_file"
    echo "Added nvs_flash dependency to CMakeLists.txt"
fi

component_manifest="$repo_root/vendor/StackChan/firmware/main/idf_component.yml"
if [[ -f "$component_manifest" ]] && ! grep -q 'esp_websocket_client' "$component_manifest"; then
    sed -i '' '/^dependencies:/a\
  espressif/esp_websocket_client: ~1.5.0
' "$component_manifest"
    echo "Added esp_websocket_client dependency to idf_component.yml"
fi
