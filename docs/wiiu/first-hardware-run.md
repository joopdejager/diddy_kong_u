# First run on a Wii U (or Cemu)

Everything below the GPU has been tested on a big-endian CPU
([big-endian-test.md](big-endian-test.md)). What hasn't run yet is the
console-specific layer: GX2 drawing, ProcUI, SDL's Wii U controller and audio
drivers, and the SD card paths. This page is what to try and what to send back.

## Setup

1. Aroma installed. `mdkr64.wuhb` in `sd:/wiiu/apps/`.
2. ROM at `sd:/wiiu/apps/dkr/dkr.z64` (US 1.1 / Rev 1 or EU 1.1).

## Checks, in order

| # | Do | Expect | If not |
|---|---|---|---|
| 1 | Start without the ROM | Text page: "The game ROM was not found", HOME returns to the menu | The app didn't start at all: send the Aroma crash screen |
| 2 | Start with the ROM | Splash, then the game's intro/title on TV and GamePad | Send `sd:/wiiu/apps/dkr/dkr.log` |
| 3 | Look at the title | Logo, text and 3D scene look right | Photo of the screen: wrong colours, missing textures or upside-down output point at specific parts of `gfx_gx2.c` |
| 4 | Press A / Start on the GamePad | Menus respond | Controllers: SDL's Wii U joystick driver or the mapping |
| 5 | Start a race | Track, karts, HUD, music, engine sounds | `dkr.log` and a photo |
| 6 | HOME during a race, then go back | HOME menu opens and closes; game continues | ProcUI handling in `wiiu_gfx.c` |
| 7 | HOME → Close | Back to the Wii U Menu without a freeze | Same |
| 8 | Finish a race, quit, start the app again | Records / progress kept | Save path (`sd:/wiiu/apps/dkr/`) |

## What to send back

- `sd:/wiiu/apps/dkr/dkr.log` (overwritten on every start; copy it before
  starting again).
- If it crashed: the text on the crash screen. It lists `PC`, `LR` and
  `Callers` addresses, and `main is at`. With `build-wiiu/mdkr64.elf` from the
  same build these turn into function names (see "Reading a crash" in
  [wiiu-build.md](wiiu-build.md)).
- A photo when something looks wrong.

## Likely first problems, and where to look

- **Black screen but music plays:** the game runs, GX2 output doesn't appear.
  `wiiu_gfx.c` (scan buffers, `GX2SetTVEnable`), or the shaders
  (`gx2_shader_gen.c`).
- **Picture upside down or offset:** viewport/scissor flip in `gfx_gx2.c`.
- **Wrong colours everywhere:** texture format/component order in
  `upload_levels` (`gfx_gx2.c`).
- **Some objects black or missing:** a combiner the shader generator handles
  differently; the log lists shader ids if generation fails.
- **Very slow:** check `dkr.log` for `[PACE]` lines; the per-vertex repack in
  `draw_triangles` or texture re-uploads would be the first suspects.
