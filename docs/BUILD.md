# Build & toolchain

## Target platform

**Native Linux/WSL with GCC** for day-to-day forge_core/CLI dev. Spec
§1/§4.3 name Windows + MSVC; both are overridden. MSVC-the-IDE was
rejected outright. Output `.abc` is plain text and platform-agnostic,
so the conversion math is fully provable on Linux.

**Local Windows cross-compile is also available** (`./build-windows.sh`),
specifically for Songsmith GUI/pixel-comparison test work that can't be
verified on Linux — see below. This supersedes the earlier "MinGW
abandoned, Windows is CI-only" position: a plain MinGW-w64 cross-compile
*was* tried and abandoned, because JUCE explicitly does not support
MinGW (`juce_TargetPlatform.h` has `#error "MinGW is not supported"`),
but `clang-cl` targeting `x86_64-pc-windows-msvc` against Microsoft's
real Windows SDK/CRT works — JUCE supports clang-cl as a first-class
Windows toolset (it's Projucer's "Clang for Windows" option), so this
route never hits that `#error`.

### Local Windows cross-compile setup

One-time per machine (and after reboot, since the FUSE mount doesn't
survive one): `./setup-windows-toolchain.sh`. Requires (installed
manually, once — apt needs an interactive sudo prompt this tooling
can't supply): `sudo apt install -y clang-20 lld-20 ciopfs`. The script
then:

- Installs `xwin` via `cargo` if missing.
- Runs `xwin --accept-license splat --include-debug-libs
  --disable-symlinks` to fetch Microsoft's real Windows SDK/CRT headers
  and libs (~800MB, proprietary Microsoft content — this is the
  standard way Rust/C++ cross-compilation to MSVC targets works, not a
  Forge-specific hack).
- Lowercases every filename in the splat output, then mounts it through
  `ciopfs` (a case-insensitive FUSE overlay) at `~/xwin-cache/xwin-ci`.
  This step is required: Windows headers reference each other with
  inconsistent case (e.g. `#include <Dbghelp.h>` when the real file is
  `dbghelp.h`), which real case-insensitive Windows tolerates but
  Linux's case-sensitive filesystem does not. `xwin`'s own
  `--disable-symlinks`-alternative (generating case-alias symlinks) was
  tried first and rejected: it collides with `ciopfs`'s own case
  folding and produces symlink loops.
- Uses **clang-20**, not the distro-default clang-18: MSVC's STL headers
  hard-gate on `Clang >= 19` (`yvals_core.h`'s `STL1000` static_assert).
- Initializes a Wine prefix at `~/.wine-forge` so the resulting `.exe`s
  (including `forge_tests.exe`) can actually run and be tested locally,
  not just compiled.

The cross toolchain file is `cmake/clang-msvc-toolchain.cmake`. Day-to-day
use is `./build-windows.sh [forge|forge_ui|forge_tests|all]` — for
`forge_tests`, it also runs the suite under Wine and reports pass/fail,
which is how the Windows-only pixel-comparison test failures (see
`docs/superpowers/plans/2026-09-14-songsmith-phase7-piano-roll-editing.md`
history / `HANDOFF-songsmith-phase7-windows-ci-2026-09-14.md`) get
reproduced and iterated on without needing a CI round-trip.

**This is a dev-loop accelerant, not a replacement for CI.** A local
Wine-based pass is a strong signal but CI on `windows-2022` (real
Windows, MSVC-built) is still the actual release gate — keep pushing to
`main`/opening a PR to get the authoritative green before shipping.

- CMake ≥ 3.22, C++17, Ninja.
- JUCE (submodule at `./JUCE`) — modules linked: `juce_audio_basics`,
  `juce_audio_formats`, `juce_core`. Console app via `juce_add_console_app`
  — no GUI modules.
- The `forge_ui` GUI binary additionally links `juce_gui_basics`,
  `juce_gui_extra`, and `juce_data_structures` (the last for Songsmith's
  `ValueTree`-based `SongDocument`). Linux/WSL build needs system packages
  `libfreetype-dev`, `libfontconfig-dev`, `libx11-dev`, `libxrandr-dev`,
  `libxinerama-dev`, `libxcursor-dev`, `libasound2-dev`. The CLI build
  does not need these.
- `forge_tests` also links `juce_data_structures` (its first JUCE module
  beyond what `forge_core` already brings in) to compile `SongDocument.cpp`
  and `SongModelBridge.cpp` directly into the test binary for
  `SongDocument_tests.cpp`/`SongModelBridge_tests.cpp`.
- Catch2 (submodule at `./Tests/Catch2`, tracking `devel`).
- `cmake/mingw-w64-toolchain.cmake` exists but is unused, kept only as a
  record of the abandoned MinGW attempt. `cmake/clang-msvc-toolchain.cmake`
  is the live one, used by `build-windows.sh`.

## Build / test commands

From the repo root:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

CLI binary: `build/forge_artefacts/Debug/forge`.

UI binary: `build/forge_ui_artefacts/Debug/song-smith`.

Convenience: `./run-ui.sh` builds incrementally and launches the GUI.

Run a single Catch2 test: `ctest --test-dir build -R <name> --output-on-failure`,
or invoke `build/Tests/forge_tests` directly with `"[tag]"` / `"test name"`.

First-time clone only: `git submodule update --init --recursive`.

## Windows packaging (release builds)

Pushed to `main` and built on a GitHub Actions Windows runner (real
MSVC, `windows-2022`) via `.github/workflows/windows-build.yml`. The
`.exe` artifacts attach to each workflow run; a rolling `latest` release
also publishes `forge-windows.zip`. This remains the release/CI build —
real MSVC, no clang-cl/xwin involved — kept separate from the local
cross-compile path above, which is for dev-loop iteration only.
