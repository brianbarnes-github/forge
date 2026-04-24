#!/usr/bin/env bash
# Launch converter_ui. Run from the repo root.
# Builds incrementally if anything is out of date.

set -e

cd "$(dirname "$0")"

if [[ ! -d build ]]; then
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
fi

cmake --build build --target converter_ui

exec build/converter_ui_artefacts/Debug/converter_ui "$@"
