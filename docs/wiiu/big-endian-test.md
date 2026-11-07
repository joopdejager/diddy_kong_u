# Testing on a big-endian CPU without a Wii U

## Why

The N64 and the Wii U are both **big-endian** with **32-bit pointers**: a
four-byte number is stored most significant byte first, and a pointer is four
bytes. PCs are little-endian with 8-byte pointers. Golden Balloon was written
for PCs, so wherever it reads the ROM it converts big-endian data to the PC's
order. On the Wii U that conversion must *not* happen, because the ROM's
bytes are already in the console's order.

Most of Golden Balloon's converters already check the host's byte order. Some
didn't, and a missed one doesn't crash on the spot: it silently gives the game
a byte-reversed number (a 1.0 becomes 4.6e-41, a 12 becomes 3072). So the
port needs a test that runs the real game on a real big-endian CPU.

## How

`tools/wiiu/be-test.sh` builds the same program as the Wii U app, from the
same `cmake/wiiu.cmake`, for **32-bit big-endian PowerPC Linux**, and runs it
under `qemu-ppc` with your ROM. Only two things differ from the console build:

- **Rendering:** `platform/wiiu/betest/gfx_null.c` draws nothing, but the whole
  renderer front end still runs: it interprets every display list, decodes
  every texture and builds every vertex. The null back end counts draws and
  triangles and checks every draw's vertex size against what the GX2 back end
  expects (`gx2_vertex_map.h`).
- **SDL:** there's no PowerPC SDL2 to install on an x86 Ubuntu machine, so
  `platform/wiiu/betest/sdl_stub.c` stands in: real threads, locks and clocks,
  and no window, sound or controllers.

The same configuration also builds for the host (x86-64) as `build-hosttest`.
`tools/wiiu/be_compare.py` runs a scripted route on both and compares them
frame by frame. Golden Balloon pins floating-point behaviour
(`-ffp-contract=off`) so that gameplay is bit-identical across machines; any
difference between the two runs means the big-endian build read something in
the wrong byte order.

## Running it

Needs (Ubuntu): `gcc-powerpc-linux-gnu qemu-user libsdl2-dev cmake`.

```bash
tools/wiiu/be-test.sh /path/to/dkr.z64 600
```

builds `build-betest/` and runs 600 frames. For the comparison, build the host
copy once:

```bash
cmake -S . -B build-hosttest -DMDKR_BE_TEST=ON -DMDKR_BE_TEST_SDL_INCLUDE=/usr/include/SDL2
```

```bash
cmake --build build-hosttest -j4
```

then, for example, a Time Trial race driven by the game's own AI:

```bash
python3 tools/wiiu/be_compare.py /path/to/dkr.z64 tests/input_scripts/nav_to_time_trial_race.txt 7500 MDKR_AUTOPILOT=1
```

The input scripts are Golden Balloon's test routes (`tests/input_scripts/`).

## Results (US 1.1 ROM)

| Route | Frames | Result |
|---|---|---|
| Boot to title and attract mode | 600 | Runs clean |
| `nav_to_time_trial_race` + autopilot (title → menus → Time Trial race, AI finishes) | 7500 | Racer position, clock, checkpoint and lap identical to x86-64 on all 4880 race frames; renderer draw/triangle counts identical; vertex layout matched all 1,951,428 draws |
| `race_full_3lap` + autopilot (8-racer race, 3 laps) | 14000 | see below |
| `race_2p_split` + autopilot (2-player split screen) | 9000 | see below |
| `adventure_race_loop` (hub → race → back to hub) | 17000 | see below |

Under qemu the PowerPC build runs about 60 frames per second of game time
on a 4-core cloud machine, so these take a few minutes each.

## Byte-order bugs found

| Symptom in the test | Cause | Fix |
|---|---|---|
| `[FATAL] SHIELD_DATA[0]: scale=-4.29e8 ... looks byte-reversed` at boot | The miscellaneous-asset tables (`objects.c`, `dkr_misc_swap_*`) were swapped unconditionally | Skip the swap on big-endian hosts |
| Crash in `cseq_track_byte` the first time music started | The music sequence header swap (`audio.c`) was unconditional, so track offsets pointed outside the sequence | Little-endian only |
| (found by reading, same pattern) | Sound table (`audio.c`), BSP tree (`tracks.c`), game text table (`game_text.c`), object-map body fields (`objects.c`) | Little-endian only |

Each fix is a `#if` on `__BYTE_ORDER__`, so the desktop build is unchanged.
