#!/bin/sh
# Builds the Wii U app (build-wiiu/mdkr64.wuhb) inside devkitPro's official
# Docker image, which has the whole toolchain (devkitPPC, wut, SDL2 for Wii U).
# Usage: tools/wiiu/build-wiiu.sh [extra cmake args]
#
# With devkitPro installed natively, the same build without Docker is:
#   cmake -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake -Bbuild-wiiu .
#   cmake --build build-wiiu -j4
set -eu

IMAGE="${DEVKITPPC_IMAGE:-devkitpro/devkitppc:latest}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

docker run --rm \
	-v "$ROOT:/src" -w /src \
	--user "$(id -u):$(id -g)" -e HOME=/tmp \
	"$IMAGE" sh -c '
		git config --global --add safe.directory /src 2>/dev/null || true
		cmake -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/WiiU.cmake" -Bbuild-wiiu "$@" .
		cmake --build build-wiiu -j"$(nproc)"
	' sh "$@"

echo
echo "Built: $ROOT/build-wiiu/mdkr64.wuhb"
