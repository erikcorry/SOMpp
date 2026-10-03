# CMake toolchain file for the 16-bit fructus CPU, see $HOME/fructus.
#
#   cmake -B build-fructus -DCMAKE_TOOLCHAIN_FILE=cmake/fructus.cmake
#
# fructus-elf-g++ is $HOME/fructus/tools/fcc, which adds the freestanding
# libstdc++ include paths, -fno-exceptions -fno-rtti, crt0, libc and the
# simulator's linker script.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR fructus)

set(FRUCTUS_ROOT "$ENV{HOME}/fructus")
set(CMAKE_CXX_COMPILER "$ENV{HOME}/fructus-tools/bin/fructus-elf-g++")
set(CMAKE_AR "${FRUCTUS_ROOT}/build/cross-bin/fructus-elf-ar")
set(CMAKE_RANLIB "${FRUCTUS_ROOT}/build/cross-bin/fructus-elf-ranlib")
set(CMAKE_OBJDUMP "${FRUCTUS_ROOT}/build/cross-bin/fructus-elf-objdump")
set(CMAKE_NM "${FRUCTUS_ROOT}/build/cross-bin/fructus-elf-nm")

# the compiler can't link a test program without our sources
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(FRUCTUS TRUE)
