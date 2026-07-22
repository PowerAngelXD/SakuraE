#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
source_dir="$script_dir/sakurae-zed"
destination_root="/mnt/d/Coding"
destination_dir="$destination_root/sakurae-zed-dev"

source_dir="$(realpath -- "$source_dir")"
destination_root="$(realpath -- "$destination_root")"

if [[ ! -f "$source_dir/extension.toml" ]]; then
    printf 'error: SakuraE extension manifest not found: %s\n' "$source_dir/extension.toml" >&2
    exit 1
fi

if [[ "$destination_root" != "/mnt/d/Coding" ]]; then
    printf 'error: unexpected Windows destination root: %s\n' "$destination_root" >&2
    exit 1
fi

printf 'syncing: %s\n' "$source_dir"
printf 'to:      %s\n' "$destination_dir"

rm -rf -- "$destination_dir"
mkdir -p -- "$destination_dir"

# Generated build and grammar checkout directories are not needed by Zed.
tar -C "$source_dir" \
    --exclude='./target' \
    --exclude='./grammars' \
    -cf - . | tar -C "$destination_dir" -xf -

printf 'SakuraE Zed extension synchronized.\n'
