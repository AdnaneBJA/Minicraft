# Overlay of vcpkg's community x64-mingw-static triplet.
# The stock MinGW toolchain only looks for prefixed compilers
# (x86_64-w64-mingw32-gcc); CLion's bundled MinGW ships plain gcc/g++.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_ENV_PASSTHROUGH PATH)

set(VCPKG_CMAKE_SYSTEM_NAME MinGW)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../toolchains/mingw-gcc.cmake")
