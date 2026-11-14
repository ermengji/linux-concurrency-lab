# Cross-compile for 64-bit ARM (RK3588 / Raspberry Pi / any aarch64 target).
#
#   sudo apt install g++-aarch64-linux-gnu
#   cmake -S . -B build-arm \
#         -DCMAKE_TOOLCHAIN_FILE=toolchains/aarch64-linux-gnu.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-arm -j
#   file build-arm/bench_ring     # -> ELF 64-bit LSB, ARM aarch64
#
# Without a board you can still run the result:
#   sudo apt install qemu-user-static
#   qemu-aarch64-static -L /usr/aarch64-linux-gnu ./build-arm/test_ring_buffer

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Search headers/libs from the target sysroot, not the host.
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
