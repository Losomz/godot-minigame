#!/usr/bin/env sh
set -eu

wasm="bin/.web_zip/godot.wasm"
output="${wasm}.br"

command -v brotli >/dev/null 2>&1 || {
    echo "ERROR: brotli was not found in PATH." >&2
    exit 1
}

test -f "$wasm" || {
    echo "ERROR: $wasm does not exist. Build the Web template first." >&2
    exit 1
}

rm -f "$output"
brotli --force "$wasm"
node godot_process.js
test -f "$output"

echo "Created $output and patched bin/.web_zip/godot.js"
