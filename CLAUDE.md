# CLAUDE.md

Working rules for this repository: a Wii U port of Diddy Kong Racing, built on
the Golden Balloon PC port (akratch/goldenballoon) of the DavidSM64
decompilation. See `docs/wiiu/README.md` for the overview, status and notes.
Upstream's own agent notes are kept in `docs/UPSTREAM_AGENTS.md`.

## Never commit game data

- No ROMs (`*.z64`, `*.n64`, `*.v64`), extracted assets, frame dumps or audio
  dumps. `.gitignore` covers the usual names; check `git status` before every
  commit anyway.
- Keep test ROMs outside the repository.

## Code placement

- Wii U-only code goes in `platform/wiiu/`; its big-endian test stand-ins in
  `platform/wiiu/betest/`; packaging in `dist/wiiu/`; scripts in `tools/wiiu/`;
  notes in `docs/wiiu/`.
- The Wii U build is defined in `cmake/wiiu.cmake` (+ `cmake/wiiu_sources.cmake`).
  `CMakeLists.txt` only includes it. When merging upstream, compare
  `cmake/wiiu_sources.cmake` with `PLATFORM_SOURCES` in `CMakeLists.txt`.
- Keep edits to shared files (`game/`, `platform/`) minimal and behind
  `__WIIU__`, `MDKR_NULL_RENDERER`, or a `__BYTE_ORDER__` check, so upstream
  merges stay easy and the desktop build is unchanged.
- Borrowed code goes under `third_party/<project>/` with its licence (MIT,
  BSD, zlib are fine; no GPL).

## Building

Wii U (Docker, `devkitpro/devkitppc` image):

```bash
tools/wiiu/build-wiiu.sh
```

Output `build-wiiu/mdkr64.wuhb`. devkitPro's package servers block cloud IPs,
so cloud sessions must use the Docker image (start `dockerd` first if needed).

Big-endian test (needs `gcc-powerpc-linux-gnu qemu-user libsdl2-dev`):

```bash
tools/wiiu/be-test.sh /path/to/dkr.z64 600
```

## Platform facts

- The Wii U is big-endian with 32-bit pointers, like the N64. ROM data is
  already in host order; any byte swap must be skipped on big-endian hosts.
  A missed one produces silently wrong numbers; `tools/wiiu/be_compare.py`
  catches them by comparing against the x86-64 build frame by frame.
- No thread-local storage in RPX files, no `openat`/`fstatat`, and wut's
  `nanosleep` crashes on a NULL second argument.
- No OpenGL: rendering goes through GX2 (`platform/wiiu/gfx_gx2.c`), shaders
  are generated as machine code at run time (`gx2_shader_gen.c`).
- SDL2 is used for controllers and audio only; `wiiu_gfx.c` owns the screen
  and ProcUI (HOME menu).
