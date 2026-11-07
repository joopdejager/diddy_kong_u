# Wii U build of the engine.
#
# Included from the top of CMakeLists.txt when the devkitPro Wii U toolchain
# file is in use (NINTENDO_WIIU is set by $DEVKITPRO/cmake/WiiU.cmake). It
# builds one target, mdkr64, and packages it as mdkr64.rpx + mdkr64.wuhb.
#
# Kept separate from the desktop build so upstream Golden Balloon changes to
# CMakeLists.txt merge without conflicts. What differs from the desktop build:
#   - no OpenGL / WebGPU: rendering goes through the GX2 back end in
#     platform/wiiu/gfx_gx2.c, and the screen is owned by platform/wiiu/wiiu_gfx.c
#   - no ImGui app shell, no online play, no HarfBuzz text shaping
#   - SDL2 is used only for audio and controllers
#   - the ROM and saves live on the SD card (sd:/wiiu/apps/dkr/)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# MDKR_BE_TEST: the same program built for 32-bit big-endian PowerPC Linux
# and run under qemu, with a renderer that draws nothing. It checks that the
# game and the renderer front end work on a big-endian, 32-bit CPU like the
# Wii U's, using a real ROM, without needing a console. SDL2 headers come from
# the host and SDL itself is replaced by small stand-ins (sdl_stub.c).
# See docs/wiiu/big-endian-test.md.
option(MDKR_BE_TEST "Build the big-endian Linux test program instead" OFF)

if(NOT MDKR_BE_TEST)
    find_package(SDL2 REQUIRED)
endif()

file(GLOB GAME_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/game/src/*.c
    ${CMAKE_SOURCE_DIR}/game/src/hasm/*.c
    ${CMAKE_SOURCE_DIR}/game/src/hasm_native/*.c)

include(${CMAKE_SOURCE_DIR}/cmake/wiiu_sources.cmake)

# Renderer front end: every fast3d TU except the desktop back ends and the
# HarfBuzz-based custom-name renderer (replaced by a stub on the Wii U).
file(GLOB FAST3D_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/platform/fast3d/*.c)
list(REMOVE_ITEM FAST3D_SOURCES
    ${CMAKE_SOURCE_DIR}/platform/fast3d/screenshot_series.c
    ${CMAKE_SOURCE_DIR}/platform/fast3d/gfx_opengl.c
    ${CMAKE_SOURCE_DIR}/platform/fast3d/gfx_character_text.c
    ${CMAKE_SOURCE_DIR}/platform/fast3d/gfx_webgpu.c
    ${CMAKE_SOURCE_DIR}/platform/fast3d/gfx_webgpu_async_request.c
    ${CMAKE_SOURCE_DIR}/platform/fast3d/gfx_webgpu_callback_latch.c
    ${CMAKE_SOURCE_DIR}/platform/fast3d/gfx_webgpu_fault.c
    ${CMAKE_SOURCE_DIR}/platform/fast3d/gfx_webgpu_surface_policy.c
    ${CMAKE_SOURCE_DIR}/platform/fast3d/gfx_webgpu_shader.c)

if(MDKR_BE_TEST)
    file(GLOB WIIU_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/platform/wiiu/betest/*.c)
    list(APPEND WIIU_SOURCES
        ${CMAKE_SOURCE_DIR}/platform/wiiu/gfx_character_text_stub.c
        ${CMAKE_SOURCE_DIR}/platform/wiiu/gfx_opengl_stubs.c)
else()
    file(GLOB WIIU_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/platform/wiiu/*.c)
endif()

set(WIIU_THIRD_PARTY_SOURCES
    ${CMAKE_SOURCE_DIR}/lib/stb/stb_image_impl.c
    ${CMAKE_SOURCE_DIR}/lib/miniz/miniz.c
    ${CMAKE_SOURCE_DIR}/lib/dr_libs/dr_wav_impl.c)
set_source_files_properties(${WIIU_THIRD_PARTY_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

set(MDKR_TARGET mdkr64)
add_executable(${MDKR_TARGET}
    ${GAME_SOURCES}
    ${WIIU_PLATFORM_SOURCES}
    ${FAST3D_SOURCES}
    ${WIIU_SOURCES}
    ${WIIU_THIRD_PARTY_SOURCES})

target_include_directories(${MDKR_TARGET} PRIVATE
    ${CMAKE_SOURCE_DIR}/platform/wiiu/compat
    ${CMAKE_SOURCE_DIR}/game
    ${CMAKE_SOURCE_DIR}/game/src
    ${CMAKE_SOURCE_DIR}/game/include
    ${CMAKE_SOURCE_DIR}/game/libultra
    ${CMAKE_SOURCE_DIR}/game/libultra/src/audio
    ${CMAKE_SOURCE_DIR}/platform
    ${CMAKE_SOURCE_DIR}/platform/fast3d
    ${CMAKE_SOURCE_DIR}/platform/fast3d_shim
    ${CMAKE_SOURCE_DIR}/platform/wiiu
    ${CMAKE_SOURCE_DIR}/lib/stb
    ${CMAKE_SOURCE_DIR}/lib/miniz
    ${CMAKE_SOURCE_DIR}/lib/dr_libs)
# The decomp includes its PR headers by basename ("sched.h"); keep them on the
# quote-only path so they never shadow the C library's <sched.h> and friends.
target_compile_options(${MDKR_TARGET} PRIVATE
    -iquote${CMAKE_SOURCE_DIR}/game/include/PR
    -iquote${CMAKE_SOURCE_DIR}/game/include/sys)

set(MDKR_VERSION "1.8.0-wiiu" CACHE STRING "release version")
target_compile_definitions(${MDKR_TARGET} PRIVATE
    VERSION_us_v80
    _LANGUAGE_C
    MODERN_CC
    NON_MATCHING=1
    AVOID_UB=1
    NATIVE_PORT=1
    F3DDKR_GBI
    _FINALROM
    SDL_MAIN_HANDLED
    STBI_NO_THREAD_LOCALS
    MDKR_WIIU=1
    MDKR_WIIU_APP_DIR="fs:/vol/external01/wiiu/apps/dkr"
    MDKR_VERSION_STRING="${MDKR_VERSION}")

target_compile_options(${MDKR_TARGET} PRIVATE
    -fno-strict-aliasing
    -ffp-contract=off
    -fcommon
    -Wno-incompatible-pointer-types
    -Wno-int-conversion
    -Wno-implicit-function-declaration
    -Werror=return-type)

if(MDKR_BE_TEST)
    target_compile_definitions(${MDKR_TARGET} PRIVATE MDKR_NULL_RENDERER=1
        SDL_DISABLE_MMINTRIN_H SDL_DISABLE_XMMINTRIN_H SDL_DISABLE_EMMINTRIN_H
        SDL_DISABLE_PMMINTRIN_H SDL_DISABLE_IMMINTRIN_H SDL_DISABLE_ARM_NEON_H)
    target_include_directories(${MDKR_TARGET} PRIVATE
        ${CMAKE_BINARY_DIR}/sdl_config
        ${MDKR_BE_TEST_SDL_INCLUDE})
    target_link_libraries(${MDKR_TARGET} PRIVATE m pthread)
    return()
endif()

target_link_libraries(${MDKR_TARGET} PRIVATE SDL2::SDL2 m)

wut_create_rpx(${MDKR_TARGET})
wut_create_wuhb(${MDKR_TARGET}
    NAME "Diddy Kong Racing"
    SHORTNAME "DKR"
    AUTHOR "diddy_kong_u"
    ICON "${CMAKE_SOURCE_DIR}/dist/wiiu/icon.png"
    TVSPLASH "${CMAKE_SOURCE_DIR}/dist/wiiu/tv-splash.png"
    DRCSPLASH "${CMAKE_SOURCE_DIR}/dist/wiiu/drc-splash.png")
