# Building the Wii U app

## With Docker (recommended, and the only way in cloud sessions)

devkitPro's official image has the whole toolchain: devkitPPC (the PowerPC
compiler), wut (the Wii U homebrew library) and SDL2 for Wii U.

```bash
tools/wiiu/build-wiiu.sh
```

Output: `build-wiiu/mdkr64.wuhb` (and `mdkr64.rpx`, `mdkr64.elf`).
Extra arguments go to CMake, e.g. `tools/wiiu/build-wiiu.sh -DCMAKE_BUILD_TYPE=Debug`.

devkitPro's package servers refuse connections from cloud data centres, so
installing devkitPro with `pacman` inside a cloud session fails; the Docker
image works because it comes from Docker Hub.

## With devkitPro installed natively

```bash
cmake -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake -Bbuild-wiiu .
```

```bash
cmake --build build-wiiu -j4
```

Packages needed: `wiiu-dev wiiu-sdl2`.

## How the build is arranged

`CMakeLists.txt` is Golden Balloon's, unchanged except for five lines at the top:
when the Wii U toolchain is active (`NINTENDO_WIIU`) it includes
`cmake/wiiu.cmake` and stops. Upstream changes to the desktop build therefore
merge without touching the Wii U build. The cost is that the list of platform
source files is duplicated in `cmake/wiiu_sources.cmake`; when merging
upstream, compare it with `PLATFORM_SOURCES` in `CMakeLists.txt`.

`cmake/wiiu.cmake` builds every file in `game/`, the platform files, the
renderer front end, and `platform/wiiu/`, then calls wut's
`wut_create_rpx` and `wut_create_wuhb`.

Left out compared to the desktop build:

| Desktop piece | Why it's out | Replacement |
|---|---|---|
| `gfx_opengl.c`, `gfx_webgpu*.c` | No OpenGL or WebGPU on the Wii U | `platform/wiiu/gfx_gx2.c` |
| ImGui app shell (`platform/app/`) | C++ desktop UI, mouse/keyboard driven | Game starts straight into the game; settings live in `sd:/wiiu/apps/dkr/` |
| `gfx_character_text.c` | Needs HarfBuzz (C++) for custom character names | `gfx_character_text_stub.c`: retail font only |
| Online play, Phone Party | Need desktop networking libraries | Not built |
| Speech (accessibility) | No speech engine | Null back end |

## The pieces

| Piece | What it is |
|---|---|
| `.elf` | What the compiler produces, the same format Linux uses. Keep it: it maps crash addresses to source lines. |
| `.rpx` | The Wii U's executable format, converted from the `.elf` by `elf2rpl`. |
| `.wuhb` | Wii U Homebrew Bundle: the `.rpx` plus name, icon and splash screens (`dist/wiiu/`). Aroma lists these on the Wii U Menu. |
| wut | Wii U homebrew library: C library, file access, and headers for the console's system libraries (coreinit, GX2, ProcUI, VPAD...). |
| SDL2 for Wii U | Used here for controllers and audio only. Its video driver is not started, because the GX2 back end sets up the graphics itself. |

## Things the Wii U toolchain doesn't have

Each of these broke the first build; each fix is behind `__WIIU__`:

| Missing | Where it was used | Fix |
|---|---|---|
| Thread-local storage (`__thread`) | `gfx_ptr.h`, stb_image | `elf2rpl` rejects TLS relocations (types 70/72). Made plain globals on the Wii U: the only writers are worker threads the Wii U build doesn't start. |
| `execinfo.h` / `backtrace()` | Crash handler in `main_pc.c` | Prints the signal only |
| `getrusage` fields | `host_sched_probe.c` | Zeroes, like Windows |
| `openat`, `fstatat`, `fdopendir`, `dirfd` | Texture-pack folder reading, directory listing | Use the pathname-based code the Windows build already has |
| `posix_memalign`, `flock` | Memory arena, save-file locking | `platform/wiiu/wiiu_compat.c` (one app runs at a time, so locking is a no-op) |

## Paths on the console

wut names the SD card `fs:/vol/external01/`. The app folder is
`fs:/vol/external01/wiiu/apps/dkr/` (`MDKR_WIIU_APP_DIR` in `cmake/wiiu.cmake`).
`platform/user_paths.c` treats the Wii U as always "portable", so settings and
saves go there too. `wiiu_main.c` sends stdout and stderr to `dkr.log` in it.

## Reading a crash

Keep `build-wiiu/mdkr64.elf` from the same build. An address from a crash
(Aroma's crash screen, or Cemu's log) maps to a function and line with:

```bash
docker run --rm -v $PWD:/src devkitpro/devkitppc sh -c '$DEVKITPRO/devkitPPC/bin/powerpc-eabi-addr2line -f -C -e /src/build-wiiu/mdkr64.elf 0x02012345'
```
