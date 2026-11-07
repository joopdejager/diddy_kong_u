# The GX2 renderer

The game never draws anything itself. Each frame it builds a **display list**,
a list of N64 graphics commands ("load these vertices", "use this texture",
"draw these triangles"), and hands it to the graphics chip. On a PC, Golden
Balloon's renderer front end (`platform/fast3d/gfx_pc_dkr.c`) interprets the
list and calls a back end, OpenGL or WebGPU, through a table of functions
(`struct GfxRenderingAPI` in `gfx_rendering_api.h`).

The Wii U back end is `platform/wiiu/gfx_gx2.c`. The front end is unchanged.

```
display list ─► gfx_pc_dkr.c ─► gfx_gx2_api ─► GX2 ─► 1280x720 colour buffer (MEM1)
 (game)          (shared)        (Wii U)                 │
                                                         ├─► TV scan buffer
                                                         └─► GamePad scan buffer
```

## Shaders without a compiler

The N64's colour combiner can be set up thousands of ways, and the front end
asks for a shader per combination it meets. OpenGL compiles GLSL text at run
time. The Wii U has no shader compiler on board (Cemu and homebrew normally
ship precompiled `.gsh` files), so `gx2_shader_gen.c` writes the GPU's own
machine code (AMD R600-family instructions) directly: a pixel shader per
combiner, a pass-through vertex shader and a fetch shader that reads the
vertex attributes. This generator comes from the Wii U port of libultraship
(GaryOderNichts, MIT), where it runs Ship of Harkinian.

Supported: one- and two-cycle combiners, two textures, texture clamping done in
the shader, fog, alpha, texture-edge cutout, noise.

Not supported, and simply left out of the picture: Golden Balloon's
Remastered-only extras (sun shadows, smooth per-pixel lighting, world-space
effects), shader-side N64 tile masking and 3-point filtering, and the
framebuffer-feedback blending variants. These fall back to plain alpha blending
and bilinear filtering, which is what most PC ports of N64 games do.

## Vertex layout

The front end packs each vertex as tightly as the GL shader expects, and the
layout changes with the combiner (`gx2_vertex_map.h` documents it). The
generated shaders read a fixed layout of four-float slots instead. So each
shader gets a small copy plan, worked out once when it's created, and
`draw_triangles` repacks every batch with it into a per-frame vertex ring. For
an N64 game that's at most a few thousand vertices a frame.

The big-endian test build checks the copy plan against every real draw call:
if the front end ever sends a vertex size the plan doesn't expect, it says so.
In the Time Trial race test, every draw matched.

## Coordinates and state

- Viewport and scissor arrive in GL's convention (origin bottom left) and are
  flipped for GX2 (origin top left).
- Depth: clip-space z is -1..1 as in GL, with depth clipping off (like
  `GL_DEPTH_CLAMP`); the front end already clips triangles on the CPU.
  Comparisons are `LEQUAL` like the GL back end, and decals (`ZMODE_DEC`) get a
  polygon offset.
- Texture ids are pointers to a `struct Texture` (pointers are 32-bit here).
  Textures are uploaded as linear RGBA8.
- The colour and depth buffers live in MEM1, the 32 MB of fast graphics memory.
  ProcUI takes MEM1 away while the HOME menu is open, which is why
  `wiiu_gfx.c` keeps its own heap there.

## Frame flow

1. `start_frame`: wait until the previous frame has been shown, clear, reset
   viewport/scissor/blend/depth.
2. The front end issues draws.
3. `end_frame`: copy the colour buffer to the TV and GamePad scan buffers
   (GX2 scales it to each screen's size).
4. `platform_sdl_present` → `wiiu_gfx_present`: swap, flush, restore the GX2
   context state.

The renderer is pinned to 1x render scale and no MSAA on the Wii U (the
desktop default supersamples at 2x), via `--video-set` arguments added in
`wiiu_main.c`.
