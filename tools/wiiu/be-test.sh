#!/bin/bash
# Big-endian test: builds the game for 32-bit big-endian PowerPC Linux (the
# same byte order and pointer size as the Wii U) and runs it under qemu with
# your ROM for a number of frames. See docs/wiiu/big-endian-test.md.
#
# Needs (Ubuntu): gcc-powerpc-linux-gnu qemu-user libsdl2-dev cmake
# Usage: tools/wiiu/be-test.sh path/to/rom.z64 [frames] [extra game args...]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build-betest"
ROM="${1:?usage: $0 ROM [frames]}"
FRAMES="${2:-600}"
shift $(( $# >= 2 ? 2 : 1 ))

mkdir -p "$BUILD/sdl_config/SDL2"
# SDL's headers are the host's; only its configuration header is 64-bit
# specific, so use a copy that says pointers are 4 bytes.
REAL_CFG="$(ls /usr/include/*/SDL2/_real_SDL_config.h | head -1)"
sed 's/#define SIZEOF_VOIDP 8/#define SIZEOF_VOIDP 4/' "$REAL_CFG" > "$BUILD/sdl_config/SDL2/_real_SDL_config.h"

cat > "$BUILD/toolchain.cmake" <<TC
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR powerpc)
set(CMAKE_C_COMPILER powerpc-linux-gnu-gcc)
set(CMAKE_FIND_ROOT_PATH /usr/powerpc-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
TC

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_TOOLCHAIN_FILE="$BUILD/toolchain.cmake" \
	-DMDKR_BE_TEST=ON -DMDKR_BE_TEST_SDL_INCLUDE=/usr/include/SDL2 \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
cmake --build "$BUILD" -j"$(nproc)"

cd "$BUILD"
exec qemu-ppc -L /usr/powerpc-linux-gnu ./mdkr64 --rom "$ROM" --headless-frames "$FRAMES" "$@"
