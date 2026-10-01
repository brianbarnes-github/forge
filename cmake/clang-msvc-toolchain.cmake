# Cross-compile Forge for Windows from Linux using clang-cl + lld-link
# against Microsoft's real Windows SDK/CRT (fetched via `xwin`), instead of
# MinGW. JUCE explicitly rejects MinGW headers (__MINGW32__ #error in
# juce_TargetPlatform.h) but supports clang-cl as a first-class Windows
# toolset (Projucer's "Clang for Windows" option), so this route avoids
# that block entirely by presenting an MSVC-compatible ABI/headers.
#
# Prerequisites:
#  1. `xwin --accept-license splat --include-debug-libs --disable-symlinks
#      --output <XWIN_CACHE_backing>` fetched the SDK/CRT. All filenames in
#      that tree must be lowercased first (Windows headers are referenced
#      with inconsistent case, e.g. `<Dbghelp.h>` vs the real `dbghelp.h` —
#      fine on real case-insensitive Windows, fatal on case-sensitive Linux).
#  2. `ciopfs <XWIN_CACHE_backing> <XWIN_CACHE>` mounts a case-insensitive
#      FUSE overlay at XWIN_CACHE — point this variable at the MOUNT, not
#      the backing dir. clang-cl 18 fails the MSVC STL's compiler-version
#      gate (needs Clang >= 19); use the clang-20 packages.
#
#   cmake -B build-windows -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/clang-msvc-toolchain.cmake \
#     -DXWIN_CACHE=$HOME/xwin-cache/xwin-ci \
#     -DCMAKE_BUILD_TYPE=Release

if(NOT DEFINED XWIN_CACHE)
  if(DEFINED ENV{XWIN_CACHE})
    set(XWIN_CACHE "$ENV{XWIN_CACHE}")
  else()
    set(XWIN_CACHE "$ENV{HOME}/xwin-cache/xwin")
  endif()
endif()

if(NOT EXISTS "${XWIN_CACHE}/sdk/include/um")
  message(FATAL_ERROR
    "XWIN_CACHE (${XWIN_CACHE}) doesn't look like an xwin splat output. "
    "Run: xwin --accept-license splat --output ${XWIN_CACHE}")
endif()

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(CMAKE_C_COMPILER clang-cl-20)
set(CMAKE_CXX_COMPILER clang-cl-20)
set(CMAKE_LINKER lld-link-20)
set(CMAKE_AR llvm-lib-20)
set(CMAKE_RC_COMPILER llvm-rc-20)
set(CMAKE_MT llvm-mt-20)

set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

set(CMAKE_FIND_ROOT_PATH "${XWIN_CACHE}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(_xwin_includes
  "${XWIN_CACHE}/crt/include"
  "${XWIN_CACHE}/sdk/include/ucrt"
  "${XWIN_CACHE}/sdk/include/um"
  "${XWIN_CACHE}/sdk/include/shared"
  "${XWIN_CACHE}/sdk/include/winrt"
  "${XWIN_CACHE}/sdk/include/cppwinrt"
)

set(_xwin_libpaths
  "${XWIN_CACHE}/crt/lib/x86_64"
  "${XWIN_CACHE}/sdk/lib/um/x86_64"
  "${XWIN_CACHE}/sdk/lib/ucrt/x86_64"
)

set(_xwin_include_flags "")
foreach(_inc ${_xwin_includes})
  string(APPEND _xwin_include_flags " /imsvc\"${_inc}\"")
endforeach()

set(_xwin_libpath_flags "")
foreach(_lib ${_xwin_libpaths})
  string(APPEND _xwin_libpath_flags " /libpath:\"${_lib}\"")
endforeach()

set(CMAKE_C_FLAGS_INIT "${_xwin_include_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_xwin_include_flags}")
set(CMAKE_RC_FLAGS_INIT "${_xwin_include_flags}")

set(CMAKE_EXE_LINKER_FLAGS_INIT "${_xwin_libpath_flags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_xwin_libpath_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_xwin_libpath_flags}")
