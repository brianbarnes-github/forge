#!/usr/bin/env bash
# Launch forge_ui. Run from the repo root.
# Builds incrementally if anything is out of date.

set -e

cd "$(dirname "$0")"

if [[ ! -d build ]]; then
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
fi

cmake --build build --target forge_ui

exec build/forge_ui_artefacts/Debug/forge_ui "$@"
