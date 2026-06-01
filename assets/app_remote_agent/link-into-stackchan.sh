#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$app_dir"

while [[ "$repo_root" != "/" && ! -d "$repo_root/vendor/StackChan/firmware/main/apps" ]]; do
  repo_root="$(dirname "$repo_root")"
done

if [[ "$repo_root" == "/" ]]; then
  echo "Could not find vendor/StackChan/firmware/main/apps above $app_dir" >&2
  exit 1
fi

target_dir="$repo_root/vendor/StackChan/firmware/main/apps"
link_path="$target_dir/app_remote_agent"

if [[ ! -d "$target_dir" ]]; then
  echo "StackChan firmware apps directory not found: $target_dir" >&2
  exit 1
fi

if [[ -L "$link_path" ]]; then
  rm -f "$link_path"
  mkdir -p "$link_path"
elif [[ -e "$link_path" ]]; then
  rm -f "$link_path/app_remote_agent.cpp" \
        "$link_path/app_remote_agent.h" \
        "$link_path/stacky_wake_word.cpp" \
        "$link_path/stacky_wake_word.h" \
        "$link_path/stacky_wake_word_model.h"
else
  mkdir -p "$link_path"
fi

ln -s "$app_dir/app_remote_agent.cpp" "$link_path/app_remote_agent.cpp"
ln -s "$app_dir/app_remote_agent.h" "$link_path/app_remote_agent.h"
ln -s "$app_dir/stacky_wake_word.cpp" "$link_path/stacky_wake_word.cpp"
ln -s "$app_dir/stacky_wake_word.h" "$link_path/stacky_wake_word.h"
ln -s "$app_dir/stacky_wake_word_model.h" "$link_path/stacky_wake_word_model.h"
echo "Linked app_remote_agent sources into $link_path"
