#!/usr/bin/env bash
# Configure/build Forge for Windows locally via clang-cl cross-compile, and
# (for the test target) run the result under Wine. Run
# ./setup-windows-toolchain.sh once first.
#
# Usage:
#   ./build-windows.sh [forge|forge_ui|forge_tests|all] [test-args...]
#
# Examples:
#   ./build-windows.sh                       # builds everything
#   ./build-windows.sh forge_tests           # build + run full suite under Wine
#   ./build-windows.sh forge_tests "[piano-roll]"   # build + run one tag

set -euo pipefail
cd "$(dirname "$0")"

XWIN_MOUNT="${XWIN_MOUNT:-$HOME/xwin-cache/xwin-ci}"
WINEPREFIX="${WINEPREFIX:-$HOME/.wine-forge}"
BUILD_DIR="build-windows"

if ! mountpoint -q "$XWIN_MOUNT" 2>/dev/null; then
  echo "xwin ciopfs mount not found at $XWIN_MOUNT. Run ./setup-windows-toolchain.sh first." >&2
  exit 1
fi

TARGET="${1:-all}"
shift || true

if [[ ! -d "$BUILD_DIR" ]]; then
  cmake -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/clang-msvc-toolchain.cmake \
    -DXWIN_CACHE="$XWIN_MOUNT" \
    -DCMAKE_BUILD_TYPE=Release
fi

case "$TARGET" in
  all)
    cmake --build "$BUILD_DIR" --parallel
    ;;
  forge|forge_ui|forge_tests)
    cmake --build "$BUILD_DIR" --target "$TARGET" --parallel
    ;;
  *)
    echo "Unknown target '$TARGET' (expected forge, forge_ui, forge_tests, or all)" >&2
    exit 1
    ;;
esac

if [[ "$TARGET" == "forge_tests" ]]; then
  WINEPREFIX="$WINEPREFIX" WINEDEBUG=-all wine "$BUILD_DIR/Tests/forge_tests.exe" --reporter compact "$@"
fi
