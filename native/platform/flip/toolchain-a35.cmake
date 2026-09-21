set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(FLIP_TOOLCHAIN "$ENV{FLIP_TOOLCHAIN}" CACHE PATH "AArch64 GNU SDK root")
if(NOT EXISTS "${FLIP_TOOLCHAIN}/bin/aarch64-linux-gcc")
  message(FATAL_ERROR "Set FLIP_TOOLCHAIN to an extracted AArch64 GNU SDK")
endif()
set(CMAKE_C_COMPILER "${CMAKE_CURRENT_LIST_DIR}/cc")
set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_LIST_DIR}/cxx")
set(CMAKE_C_COMPILER_TARGET aarch64-buildroot-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-buildroot-linux-gnu)
set(CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN "${FLIP_TOOLCHAIN}")
set(CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN "${FLIP_TOOLCHAIN}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=${FLIP_TOOLCHAIN}/bin/aarch64-linux-ld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=${FLIP_TOOLCHAIN}/bin/aarch64-linux-ld")
set(CMAKE_SYSROOT "${FLIP_TOOLCHAIN}/aarch64-buildroot-linux-gnu/sysroot")
# +nocrypto: the ARMv8 crypto extension (AES/SHA/PMULL) is OPTIONAL on Cortex-A35, but clang enables it by
# default for -mcpu=cortex-a35 and abseil then emits AES in its hash. This binary is the universal PortMaster
# baseline and must run on crypto-less aarch64 CPUs too (e.g. Raspberry Pi 4 / BCM2712 Cortex-A72 exposes
# fp/asimd/crc32 but NOT aes), where those instructions SIGILL. crc32 stays enabled.
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-a35+nocrypto")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-a35+nocrypto")
set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
# Extra AArch64 root with the Wayland/xkbcommon client libraries SDL resolves sonames from
# (prepare_wayland.py); SDL only dlopens them at run time.
set(FLIP_WAYLAND_ROOT "$ENV{FLIP_WAYLAND_ROOT}" CACHE PATH "AArch64 Wayland client library root")
if(FLIP_WAYLAND_ROOT)
  list(APPEND CMAKE_FIND_ROOT_PATH "${FLIP_WAYLAND_ROOT}")
endif()
# The SDL3-over-SDL2 shim install (native/tools/build_sdl3_shim.sh) when the game links SDL3 shared.
set(FLIP_SDL3_ROOT "$ENV{FLIP_SDL3_ROOT}" CACHE PATH "AArch64 SDL3 (SDL2-backend shim) install prefix")
if(FLIP_SDL3_ROOT)
  list(APPEND CMAKE_FIND_ROOT_PATH "${FLIP_SDL3_ROOT}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
