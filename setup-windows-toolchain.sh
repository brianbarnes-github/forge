#!/usr/bin/env bash
# One-time (and idempotent) setup for local Windows cross-compilation via
# clang-cl + xwin + a case-insensitive ciopfs overlay. Run this once per
# machine / per reboot (the ciopfs FUSE mount doesn't survive a reboot),
# then use build-windows.sh to configure/build/test.
#
# Why this exists instead of MinGW: JUCE hard-rejects MinGW
# (juce_TargetPlatform.h #errors on __MINGW32__). clang-cl targeting
# x86_64-pc-windows-msvc against Microsoft's real SDK/CRT (fetched via
# `xwin`) sidesteps that, since JUCE supports clang-cl as a first-class
# Windows toolset. See docs/BUILD.md for the full story.
#
# Requires (install once, apt cannot be run non-interactively from here):
#   sudo apt install -y clang-20 lld-20 ciopfs
# `cargo` must be on PATH (used to install `xwin` if missing).

set -euo pipefail

XWIN_BACKING="${XWIN_BACKING:-$HOME/xwin-cache/xwin}"
XWIN_MOUNT="${XWIN_MOUNT:-$HOME/xwin-cache/xwin-ci}"
WINEPREFIX="${WINEPREFIX:-$HOME/.wine-forge}"

need() { command -v "$1" >/dev/null 2>&1 || { echo "Missing required tool: $1" >&2; exit 1; }; }

need clang-cl-20
need lld-link-20
need llvm-lib-20
need llvm-rc-20
need llvm-mt-20
need ciopfs
need wine
need cmake
need ninja

if ! command -v xwin >/dev/null 2>&1; then
  need cargo
  echo "Installing xwin via cargo..."
  cargo install xwin --locked
fi
XWIN="$(command -v xwin || echo "$HOME/.cargo/bin/xwin")"

if [[ ! -d "$XWIN_BACKING/sdk/include/um" ]]; then
  echo "Fetching Windows SDK/CRT via xwin into $XWIN_BACKING (~800MB download)..."
  "$XWIN" --accept-license --cache-dir "$HOME/xwin-cache/dl-cache" \
    splat --include-debug-libs --disable-symlinks --output "$XWIN_BACKING"

  echo "Lowercasing filenames (Windows headers are referenced with"
  echo "inconsistent case; ciopfs needs a consistently-lowercase backing"
  echo "store to fold case correctly)..."
  python3 - "$XWIN_BACKING" <<'EOF'
import os, sys
root = sys.argv[1]
for dirpath, dirnames, filenames in os.walk(root, topdown=False):
    for name in filenames + dirnames:
        lower = name.lower()
        if lower != name:
            src = os.path.join(dirpath, name)
            dst = os.path.join(dirpath, lower)
            if os.path.exists(dst) and os.path.realpath(dst) != os.path.realpath(src):
                continue
            os.rename(src, dst)
EOF
else
  echo "xwin splat output already present at $XWIN_BACKING (skipping fetch)."
fi

mkdir -p "$XWIN_MOUNT"
if mountpoint -q "$XWIN_MOUNT"; then
  echo "ciopfs already mounted at $XWIN_MOUNT."
else
  echo "Mounting ciopfs case-insensitive overlay at $XWIN_MOUNT..."
  ciopfs "$XWIN_BACKING" "$XWIN_MOUNT"
fi

if [[ ! -d "$WINEPREFIX" ]]; then
  echo "Initializing Wine prefix at $WINEPREFIX..."
  WINEPREFIX="$WINEPREFIX" WINEARCH=win64 WINEDEBUG=-all wineboot --init >/dev/null 2>&1 || true
fi

echo "Windows toolchain ready. Use ./build-windows.sh to build."
