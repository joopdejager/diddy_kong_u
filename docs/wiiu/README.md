# Diddy Kong Racing for Wii U

A native Wii U build of Diddy Kong Racing: the game's C code compiled for the
Wii U's PowerPC CPU, drawing through the console's own graphics API (GX2).
No N64 emulator is involved.

You need your own ROM. No ROM or game assets are, or ever will be, included in
this repository.

**Current phase:** the `.wuhb` builds and the game logic is verified on a
big-endian 32-bit CPU under qemu. Not yet run on real Wii U hardware or Cemu:
see [Status](#status).

## Where it comes from

```
┌───────────────────────────────────────────┐
│ Game code (racing, AI, menus, adventure)  │  game/            ← DavidSM64's decomp,
│ Plain C. Compiles for any CPU.            │                     as adapted by Golden Balloon
├───────────────────────────────────────────┤
│ libultra replacement, audio, input, ROM   │  platform/        ← Golden Balloon's PC port
│ loading, renderer front end (Fast3D)      │
├───────────────────────────────────────────┤
│ Wii U layer: GX2 renderer, screen, HOME   │  platform/wiiu/   ← this repository
│ menu, SD card paths, entry point          │
└───────────────────────────────────────────┘
```

- **[DavidSM64/Diddy-Kong-Racing](https://github.com/DavidSM64/Diddy-Kong-Racing)**:
  the 100% decompilation. Its code is the `game/` folder.
- **[akratch/goldenballoon](https://github.com/akratch/goldenballoon)**: the PC
  source port built on that decomp (MIT for its own code). This repository
  starts from its commit `2b69e748` and keeps its layout, so upstream fixes
  can be merged. It already converts big-endian ROM data only on
  little-endian hosts, which is what made a big-endian console target realistic.
- **[GaryOderNichts/libultraship](https://github.com/GaryOderNichts/libultraship) (`wiiu/updates`)**:
  the Wii U port of the Ship of Harkinian engine. Its GX2 shader generator
  (`platform/wiiu/gx2_shader_gen.c`) and the structure of the GX2 back end and
  screen code come from there (MIT, `third_party/libultraship_wiiu/LICENSE`).
- **[joopdejager/perfect_dark_u](https://github.com/joopdejager/perfect_dark_u)**:
  the model for the build script, Docker workflow, fiber stack and the
  big-endian-under-qemu testing idea.
- **[ThatGuyMcd/DKR-R](https://github.com/ThatGuyMcd/DKR-R)** (static recompilation)
  was looked at but not used: it depends on N64Recomp's runtime and RT64
  (Vulkan/D3D12), neither of which exists on the Wii U.

## Installing

You need a Wii U running the [Aroma](https://aroma.foryour.cafe/) homebrew
environment.

1. Copy `mdkr64.wuhb` to `sd:/wiiu/apps/` (any file name, e.g. `dkr.wuhb`).
2. Copy your ROM to `sd:/wiiu/apps/dkr/dkr.z64`. US 1.1 (Rev 1) or EU 1.1.
   `.z64`, `.v64` and `.n64` byte orders all work; the name may also be
   `baserom.us.v80.z64`.
3. Start "Diddy Kong Racing" from the Wii U Menu.

Settings, saves and the log (`dkr.log`) go in `sd:/wiiu/apps/dkr/`.
Without a ROM the app shows a page saying where to put it; press HOME to leave.

In [Cemu](https://cemu.info/): File → Load the `.wuhb`; the SD card is the
`sdcard` folder in Cemu's data folder.

## Building

See [wiiu-build.md](wiiu-build.md). Short version, with Docker:

```bash
tools/wiiu/build-wiiu.sh
```

The result is `build-wiiu/mdkr64.wuhb`. GitHub Actions also builds it on every
push (`.github/workflows/wiiu.yml`, artifact `dkr-wiiu`).

## Status

| Area | State |
|---|---|
| Builds as `.rpx` / `.wuhb` | Yes |
| Game logic on big-endian 32-bit CPU | Verified under qemu with a US 1.1 ROM: boots, menus, a full Time Trial race driven by the game's AI; racer position matches the x86-64 build exactly, frame by frame ([big-endian-test.md](big-endian-test.md)) |
| Renderer front end on big-endian | Runs in the same test; every draw call's vertex layout matches what the GX2 back end expects |
| GX2 back end (`platform/wiiu/gfx_gx2.c`) | Written, compiles, untested on hardware |
| Controllers | GamePad and Pro Controller through SDL2's Wii U joystick driver, using Golden Balloon's standard mapping |
| Audio | SDL2's Wii U audio, same code path as desktop |
| HOME menu / exit | ProcUI via `wiiu_gfx.c` |
| Not in the Wii U build | ImGui launcher/overlay, online play, custom characters, Remastered lighting/shadows, texture-pack mipmaps |

What to expect on first hardware run, and what to report: see
[first-hardware-run.md](first-hardware-run.md).

## Notes

- [wiiu-build.md](wiiu-build.md): building, the toolchain, and what each piece is.
- [gx2-renderer.md](gx2-renderer.md): how the GX2 back end works.
- [big-endian-test.md](big-endian-test.md): testing the game on a big-endian CPU without a Wii U.
- [first-hardware-run.md](first-hardware-run.md): checklist for the first console test.
