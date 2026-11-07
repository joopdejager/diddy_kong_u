/*
 * gfx_gx2.c - Fast3D rendering back end for the Wii U's GX2 graphics API.
 *
 * This takes the place of gfx_opengl.c. The renderer front end
 * (platform/fast3d/gfx_pc_dkr.c) interprets the game's display lists and
 * hands this file batches of triangles plus "which colour combiner, which
 * textures, which depth/blend mode". This file turns that into GX2 calls.
 *
 * Shaders: GX2 has no GLSL compiler built in, so shaders are generated
 * directly as GPU machine code at runtime by gx2_shader_gen.c, one per
 * colour-combiner setup, and cached.
 *
 * Vertex layout: the front end packs vertex attributes tightly in the order
 * the GL back end expects (position, optional extras, texture coordinates and
 * clamp values, fog, combiner inputs). The generated shaders read a fixed
 * layout of four-float slots instead, so draw_triangles repacks each batch.
 * DKR's scenes are N64-sized, so this costs little.
 *
 * Based on libultraship's gfx_gx2.cpp (MIT, GaryOderNichts), reworked for
 * Golden Balloon's rendering API and written in C.
 */
#ifdef __WIIU__

#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <coreinit/memory.h>
#include <gx2/clear.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <gx2/swap.h>
#include <gx2/texture.h>
#include <gx2/utils.h>

#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gx2_shader_gen.h"
#include "wiiu_gfx.h"

#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

/* N64 texture clamp/mirror bits (G_TX_*). */
#define TX_MIRROR 0x1
#define TX_CLAMP 0x2

/* Vertex data for a whole frame goes into one ring; the GPU reads it after
 * the CPU has moved on, so it is only reset once a frame has been presented. */
#define DRAW_BUFFER_SIZE (24u * 1024u * 1024u)

/* Up to 4 (pos) + 2*4 (tex) + 4 (fog) + 7*4 (inputs) floats per vertex. */
#define MAX_DST_FLOATS 48

struct CopyRun {
    uint8_t src;   /* offset in the source vertex, in floats */
    uint8_t dst;   /* offset in the repacked vertex, in floats */
    uint8_t count; /* floats to copy */
};

struct ShaderProgram {
    uint64_t id0;
    uint32_t id1;
    struct ShaderGroup group;
    bool valid;
    uint8_t num_inputs;
    bool used_textures[2];
    uint32_t window_params_offset;
    int32_t sampler_location[2];

    uint32_t src_floats; /* floats per vertex as packed by the front end */
    uint32_t dst_floats; /* floats per vertex as read by the shader */
    uint8_t num_runs;
    struct CopyRun runs[24];
    /* Slots that must be filled with a constant because the source had no
     * value for them (e.g. the alpha of a 3-component combiner input). */
    uint8_t num_ones;
    uint8_t ones[8];

    struct ShaderProgram *next;
};

struct Texture {
    GX2Texture texture;
    GX2Sampler sampler;
    bool uploaded;
    bool sampler_set;
};

#define SHADER_HASH_SIZE 256
static struct ShaderProgram *s_shaderHash[SHADER_HASH_SIZE];
static struct ShaderProgram *s_currentShader;

static struct Texture *s_currentTextures[2];
static int s_currentTile;

static GX2ColorBuffer s_colorBuffer;
static GX2DepthBuffer s_depthBuffer;

static uint8_t *s_drawBuffer;
static uint32_t s_drawOffset;

static uint32_t s_frameCount;
static float s_noiseScale = 1.0f;

static float s_viewport[4] = { 0.0f, 0.0f, WIIU_FB_WIDTH, WIIU_FB_HEIGHT };
static uint32_t s_scissor[4] = { 0, 0, WIIU_FB_WIDTH, WIIU_FB_HEIGHT };

static BOOL s_depthTest = TRUE;
static BOOL s_depthWrite = TRUE;
static GX2CompareFunction s_depthFunc = GX2_COMPARE_FUNC_LEQUAL;
static bool s_polygonOffset;
static enum GfxBlendMode s_blendMode = GFX_BLEND_DISABLED;
static enum GfxRenderingStatus s_status = GFX_RENDERING_UNINITIALIZED;
static bool s_frameActive;

/* ----------------------------------------------------------------------- */

static uint32_t shader_hash(uint64_t id0, uint32_t id1) {
    uint64_t h = id0 ^ ((uint64_t)id1 * 0x9E3779B97F4A7C15ull);
    h ^= h >> 29;
    return (uint32_t)(h ^ (h >> 17)) & (SHADER_HASH_SIZE - 1);
}

static int32_t sampler_location(const GX2PixelShader *shader, const char *name) {
    for (uint32_t i = 0; i < shader->samplerVarCount; i++) {
        if (strcmp(shader->samplerVars[i].name, name) == 0) {
            return (int32_t)shader->samplerVars[i].location;
        }
    }
    return -1;
}

static uint32_t uniform_offset(const GX2PixelShader *shader, const char *name) {
    for (uint32_t i = 0; i < shader->uniformVarCount; i++) {
        if (strcmp(shader->uniformVars[i].name, name) == 0) {
            return shader->uniformVars[i].offset;
        }
    }
    return 0;
}

static void add_run(struct ShaderProgram *prg, uint32_t src, uint32_t dst, uint32_t count) {
    if (count == 0) {
        return;
    }
    if (prg->num_runs > 0) {
        struct CopyRun *last = &prg->runs[prg->num_runs - 1];
        if (last->src + last->count == src && last->dst + last->count == dst) {
            last->count += count;
            return;
        }
    }
    if (prg->num_runs < sizeof(prg->runs) / sizeof(prg->runs[0])) {
        prg->runs[prg->num_runs].src = (uint8_t)src;
        prg->runs[prg->num_runs].dst = (uint8_t)dst;
        prg->runs[prg->num_runs].count = (uint8_t)count;
        prg->num_runs++;
    }
}

/* Work out how a vertex packed by the front end (in gfx_opengl.c's order)
 * maps onto the shader's fixed 4-float slots:
 *   pos, [tex0: s t clampS clampT], [tex1], [fog rgba], inputs (rgba each). */
static void build_vertex_map(struct ShaderProgram *prg, const struct CCFeatures *cc) {
    uint32_t src = 0;
    uint32_t dst = 0;

    add_run(prg, src, dst, 4); /* position */
    src += 4;
    dst += 4;

    if (cc->opt_alpha && cc->diag_rdp_cvg_memory_blend) {
        src += 6; /* coverage-memory diagnostics, unused here */
    }
    if (cc->opt_world_pos) {
        src += 3;
    }
    if (cc->opt_dfdx_light) {
        src += 6;
    }
    for (int i = 0; i < 2; i++) {
        if (!cc->used_textures[i]) {
            continue;
        }
        add_run(prg, src, dst, 2); /* s, t */
        src += 2;
        for (int axis = 0; axis < 2; axis++) {
            if (cc->clamp[i][axis]) {
                add_run(prg, src, dst + 2 + axis, 1);
                src += 1;
            }
            if (cc->tile_mask[i][axis]) {
                src += 1; /* shader-side mask wrap is not implemented on GX2 */
            }
        }
        dst += 4;
    }
    if (cc->opt_fog) {
        add_run(prg, src, dst, 4);
        src += 4;
        dst += 4;
    }
    for (int i = 0; i < cc->num_inputs; i++) {
        uint32_t n = cc->opt_alpha ? 4 : 3;
        add_run(prg, src, dst, n);
        if (n == 3 && prg->num_ones < sizeof(prg->ones)) {
            prg->ones[prg->num_ones++] = (uint8_t)(dst + 3);
        }
        src += n;
        dst += 4;
    }
    prg->src_floats = src;
    prg->dst_floats = dst;
}

/* ----------------------------------------------------------------------- */

static bool gfx_gx2_z_is_from_0_to_1(void) {
    return false;
}

static void gfx_gx2_set_uniforms(struct ShaderProgram *prg) {
    float params[4] = { s_noiseScale, (float)s_frameCount, 0.0f, 0.0f };
    GX2SetPixelUniformReg(prg->window_params_offset, 4, (uint32_t *)params);
}

static void gfx_gx2_unload_shader(struct ShaderProgram *old_prg) {
    (void)old_prg;
    s_currentShader = NULL;
}

static void bind_textures(void) {
    for (int i = 0; i < 2; i++) {
        struct Texture *tex = s_currentTextures[i];
        int32_t loc;
        if (s_currentShader == NULL || tex == NULL) {
            continue;
        }
        loc = s_currentShader->sampler_location[i];
        if (loc < 0) {
            continue;
        }
        if (tex->uploaded) {
            GX2SetPixelTexture(&tex->texture, (uint32_t)loc);
        }
        if (tex->sampler_set) {
            GX2SetPixelSampler(&tex->sampler, (uint32_t)loc);
        }
    }
}

static void gfx_gx2_load_shader(struct ShaderProgram *new_prg) {
    s_currentShader = new_prg;
    if (new_prg == NULL || !new_prg->valid) {
        return;
    }
    GX2SetFetchShader(&new_prg->group.fetchShader);
    GX2SetVertexShader(&new_prg->group.vertexShader);
    GX2SetPixelShader(&new_prg->group.pixelShader);
    gfx_gx2_set_uniforms(new_prg);
    bind_textures();
}

static struct ShaderProgram *gfx_gx2_lookup_shader(uint64_t shader_id0, uint32_t shader_id1) {
    struct ShaderProgram *prg = s_shaderHash[shader_hash(shader_id0, shader_id1)];
    while (prg != NULL) {
        if (prg->id0 == shader_id0 && prg->id1 == shader_id1) {
            return prg;
        }
        prg = prg->next;
    }
    return NULL;
}

static struct ShaderProgram *gfx_gx2_create_and_load_new_shader(uint64_t shader_id0,
                                                                uint32_t shader_id1) {
    struct CCFeatures cc;
    struct ShaderProgram *prg = calloc(1, sizeof(*prg));
    uint32_t bucket = shader_hash(shader_id0, shader_id1);

    if (prg == NULL) {
        return NULL;
    }
    gfx_cc_get_features(shader_id0, shader_id1, &cc);
    prg->id0 = shader_id0;
    prg->id1 = shader_id1;
    prg->num_inputs = (uint8_t)cc.num_inputs;
    prg->used_textures[0] = cc.used_textures[0];
    prg->used_textures[1] = cc.used_textures[1];
    build_vertex_map(prg, &cc);

    if (gx2GenerateShaderGroup(&prg->group, &cc) == 0) {
        prg->valid = true;
        prg->window_params_offset = uniform_offset(&prg->group.pixelShader, "window_params");
        prg->sampler_location[0] = sampler_location(&prg->group.pixelShader, "uTex0");
        prg->sampler_location[1] = sampler_location(&prg->group.pixelShader, "uTex1");
    } else {
        printf("[gx2] shader generation failed for %016llx:%08lx\n",
               (unsigned long long)shader_id0, (unsigned long)shader_id1);
    }

    /* Insert even when invalid so a broken combiner isn't regenerated every
     * draw; its triangles are just skipped. */
    prg->next = s_shaderHash[bucket];
    s_shaderHash[bucket] = prg;
    gfx_gx2_load_shader(prg);
    return prg;
}

static void gfx_gx2_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs,
                                    bool used_textures[2]) {
    *num_inputs = prg->num_inputs;
    used_textures[0] = prg->used_textures[0];
    used_textures[1] = prg->used_textures[1];
}

/* Texture ids are the Texture pointers themselves: pointers are 32-bit here. */
static uint32_t gfx_gx2_new_texture(void) {
    struct Texture *tex = calloc(1, sizeof(*tex));
    return (uint32_t)(uintptr_t)tex;
}

static void gfx_gx2_delete_texture(uint32_t texture_id) {
    struct Texture *tex = (struct Texture *)(uintptr_t)texture_id;
    if (tex == NULL) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        if (s_currentTextures[i] == tex) {
            s_currentTextures[i] = NULL;
        }
    }
    /* The GPU may still be reading it from a frame in flight. */
    GX2DrawDone();
    free(tex->texture.surface.image);
    free(tex);
}

static void gfx_gx2_select_texture(int tile, uint32_t texture_id) {
    if (tile < 0 || tile > 1) {
        return;
    }
    s_currentTile = tile;
    s_currentTextures[tile] = (struct Texture *)(uintptr_t)texture_id;
    bind_textures();
}

static bool upload_levels(const uint8_t *const *level_rgba, const int *level_w,
                          const int *level_h, int level_count) {
    struct Texture *tex = s_currentTextures[s_currentTile];
    uint32_t width;
    uint32_t height;

    if (tex == NULL || level_count <= 0 || level_w[0] <= 0 || level_h[0] <= 0) {
        return false;
    }
    width = (uint32_t)level_w[0];
    height = (uint32_t)level_h[0];

    if (tex->texture.surface.image == NULL || tex->texture.surface.width != width ||
        tex->texture.surface.height != height) {
        if (tex->texture.surface.image != NULL) {
            GX2DrawDone();
            free(tex->texture.surface.image);
        }
        memset(&tex->texture, 0, sizeof(tex->texture));
        tex->texture.surface.use = GX2_SURFACE_USE_TEXTURE;
        tex->texture.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
        tex->texture.surface.width = width;
        tex->texture.surface.height = height;
        tex->texture.surface.depth = 1;
        tex->texture.surface.mipLevels = 1;
        tex->texture.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
        tex->texture.surface.aa = GX2_AA_MODE1X;
        tex->texture.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
        tex->texture.viewFirstMip = 0;
        tex->texture.viewNumMips = 1;
        tex->texture.viewFirstSlice = 0;
        tex->texture.viewNumSlices = 1;
        tex->texture.compMap =
            GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
        GX2CalcSurfaceSizeAndAlignment(&tex->texture.surface);
        GX2InitTextureRegs(&tex->texture);
        tex->texture.surface.image =
            memalign(tex->texture.surface.alignment, tex->texture.surface.imageSize);
        if (tex->texture.surface.image == NULL) {
            tex->uploaded = false;
            return false;
        }
    }

    {
        uint8_t *dst = tex->texture.surface.image;
        const uint8_t *src = level_rgba[0];
        uint32_t pitch = tex->texture.surface.pitch * 4u;
        for (uint32_t y = 0; y < height; y++) {
            memcpy(dst + y * pitch, src + y * width * 4u, width * 4u);
        }
    }
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, tex->texture.surface.image,
                  tex->texture.surface.imageSize);
    tex->uploaded = true;
    bind_textures();
    return true;
}

static bool gfx_gx2_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
    return upload_levels(&rgba32_buf, &width, &height, 1);
}

static bool gfx_gx2_upload_texture_mipped(const uint8_t *const *level_rgba, const int *level_w,
                                          const int *level_h, int level_count) {
    /* Only the base level is kept: N64 textures are small, and linear
     * filtering of the base level looks the same as the GL back end without
     * its optional mipmaps. */
    return upload_levels(level_rgba, level_w, level_h, level_count);
}

static GX2TexClampMode cm_to_gx2(uint32_t val) {
    switch (val & (TX_MIRROR | TX_CLAMP)) {
        case TX_CLAMP:
            return GX2_TEX_CLAMP_MODE_CLAMP;
        case TX_MIRROR:
            return GX2_TEX_CLAMP_MODE_MIRROR;
        case TX_MIRROR | TX_CLAMP:
            return GX2_TEX_CLAMP_MODE_MIRROR_ONCE;
        default:
            return GX2_TEX_CLAMP_MODE_WRAP;
    }
}

static void gfx_gx2_set_sampler_parameters(int sampler, bool linear_filter, uint32_t cms,
                                           uint32_t cmt) {
    struct Texture *tex;
    if (sampler < 0 || sampler > 1) {
        return;
    }
    tex = s_currentTextures[sampler];
    if (tex == NULL) {
        return;
    }
    GX2InitSampler(&tex->sampler, GX2_TEX_CLAMP_MODE_CLAMP,
                   linear_filter ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT);
    GX2InitSamplerClamping(&tex->sampler, cm_to_gx2(cms), cm_to_gx2(cmt),
                           GX2_TEX_CLAMP_MODE_WRAP);
    tex->sampler_set = true;
    bind_textures();
}

static void apply_polygon_offset(void) {
    if (s_polygonOffset) {
        GX2SetPolygonOffset(-2.0f, -2.0f, -2.0f, -2.0f, 0.0f);
        GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, TRUE, GX2_POLYGON_MODE_TRIANGLE,
                             GX2_POLYGON_MODE_TRIANGLE, TRUE, TRUE, FALSE);
    } else {
        GX2SetPolygonOffset(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, FALSE, GX2_POLYGON_MODE_TRIANGLE,
                             GX2_POLYGON_MODE_TRIANGLE, FALSE, FALSE, FALSE);
    }
}

static void gfx_gx2_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare,
                                   bool depth_source_prim, uint16_t zmode) {
    (void)depth_source_prim;
    s_polygonOffset = false;
    if (!depth_test) {
        s_depthTest = FALSE;
        s_depthWrite = FALSE;
        s_depthFunc = GX2_COMPARE_FUNC_ALWAYS;
    } else {
        s_depthTest = TRUE;
        s_depthWrite = depth_update ? TRUE : FALSE;
        if (!depth_compare) {
            s_depthFunc = GX2_COMPARE_FUNC_ALWAYS;
        } else {
            /* Same choices as the GL back end: LEQUAL everywhere (N64 room
             * geometry has many equal-depth seams), and a polygon offset for
             * decals (ZMODE_DEC) so they sit on top of what they decorate. */
            s_depthFunc = GX2_COMPARE_FUNC_LEQUAL;
            s_polygonOffset = (zmode == 0xc00);
        }
    }
    GX2SetDepthOnlyControl(s_depthTest, s_depthWrite, s_depthFunc);
    apply_polygon_offset();
}

/* The front end uses GL's convention (origin bottom-left); GX2's is top-left. */
static void gfx_gx2_set_viewport(int x, int y, int width, int height) {
    s_viewport[0] = (float)x;
    s_viewport[1] = (float)(WIIU_FB_HEIGHT - y - height);
    s_viewport[2] = (float)width;
    s_viewport[3] = (float)height;
    GX2SetViewport(s_viewport[0], s_viewport[1], s_viewport[2], s_viewport[3], 0.0f, 1.0f);
}

static void gfx_gx2_set_scissor(int x, int y, int width, int height) {
    int top = WIIU_FB_HEIGHT - y - height;
    int right = x + width;
    int bottom = top + height;
    if (x < 0) x = 0;
    if (top < 0) top = 0;
    if (right > WIIU_FB_WIDTH) right = WIIU_FB_WIDTH;
    if (bottom > WIIU_FB_HEIGHT) bottom = WIIU_FB_HEIGHT;
    if (right < x) right = x;
    if (bottom < top) bottom = top;
    s_scissor[0] = (uint32_t)x;
    s_scissor[1] = (uint32_t)top;
    s_scissor[2] = (uint32_t)(right - x);
    s_scissor[3] = (uint32_t)(bottom - top);
    GX2SetScissor(s_scissor[0], s_scissor[1], s_scissor[2], s_scissor[3]);
}

static void gfx_gx2_set_shadow_view(int view_index) {
    (void)view_index;
}

static void apply_blend_mode(void) {
    switch (s_blendMode) {
        case GFX_BLEND_DISABLED:
            GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x00, FALSE, TRUE);
            break;
        case GFX_BLEND_MODULATE:
            GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xff, FALSE, TRUE);
            GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_DST_COLOR,
                               GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD, FALSE,
                               GX2_BLEND_MODE_ZERO, GX2_BLEND_MODE_ZERO,
                               GX2_BLEND_COMBINE_MODE_ADD);
            break;
        default:
            /* Plain alpha blending. The GL back end's RDP-memory and coverage
             * variants are framebuffer-feedback refinements of this. */
            GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xff, FALSE, TRUE);
            GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA,
                               GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD,
                               FALSE, GX2_BLEND_MODE_ZERO, GX2_BLEND_MODE_ZERO,
                               GX2_BLEND_COMBINE_MODE_ADD);
            break;
    }
}

static void gfx_gx2_set_blend_mode(enum GfxBlendMode mode) {
    s_blendMode = mode;
    apply_blend_mode();
}

static void gfx_gx2_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    struct ShaderProgram *prg = s_currentShader;
    uint32_t vertices;
    uint32_t bytes;
    float *dst;

    if (!s_frameActive || prg == NULL || !prg->valid || buf_vbo_num_tris == 0 ||
        prg->src_floats == 0) {
        return;
    }
    vertices = (uint32_t)(buf_vbo_num_tris * 3u);
    if ((size_t)vertices * prg->src_floats > buf_vbo_len) {
        vertices = (uint32_t)(buf_vbo_len / prg->src_floats);
        vertices -= vertices % 3u;
        if (vertices == 0) {
            return;
        }
    }
    bytes = vertices * prg->dst_floats * sizeof(float);
    if (s_drawOffset + bytes > DRAW_BUFFER_SIZE) {
        /* Out of room this frame: let the GPU finish so the ring can wrap. */
        GX2DrawDone();
        s_drawOffset = 0;
        if (bytes > DRAW_BUFFER_SIZE) {
            return;
        }
    }
    dst = (float *)(s_drawBuffer + s_drawOffset);
    s_drawOffset = ALIGN_UP(s_drawOffset + bytes, GX2_VERTEX_BUFFER_ALIGNMENT);

    {
        const float *src = buf_vbo;
        float *out = dst;
        for (uint32_t v = 0; v < vertices; v++) {
            memset(out, 0, prg->dst_floats * sizeof(float));
            for (uint32_t r = 0; r < prg->num_runs; r++) {
                const struct CopyRun *run = &prg->runs[r];
                memcpy(out + run->dst, src + run->src, run->count * sizeof(float));
            }
            for (uint32_t o = 0; o < prg->num_ones; o++) {
                out[prg->ones[o]] = 1.0f;
            }
            src += prg->src_floats;
            out += prg->dst_floats;
        }
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, dst, bytes);
    GX2SetAttribBuffer(0, bytes, prg->dst_floats * sizeof(float), dst);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, vertices, 0, 1);
}

static bool gfx_gx2_init(void) {
    if (!wiiu_gfx_init()) {
        s_status = GFX_RENDERING_FATAL;
        return false;
    }

    memset(&s_colorBuffer, 0, sizeof(s_colorBuffer));
    s_colorBuffer.surface.use = GX2_SURFACE_USE_TEXTURE_COLOR_BUFFER_TV;
    s_colorBuffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    s_colorBuffer.surface.width = WIIU_FB_WIDTH;
    s_colorBuffer.surface.height = WIIU_FB_HEIGHT;
    s_colorBuffer.surface.depth = 1;
    s_colorBuffer.surface.mipLevels = 1;
    s_colorBuffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    s_colorBuffer.surface.aa = GX2_AA_MODE1X;
    s_colorBuffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
    s_colorBuffer.viewNumSlices = 1;
    GX2CalcSurfaceSizeAndAlignment(&s_colorBuffer.surface);
    GX2InitColorBufferRegs(&s_colorBuffer);
    s_colorBuffer.surface.image =
        wiiu_gfx_alloc_mem1(s_colorBuffer.surface.imageSize, s_colorBuffer.surface.alignment);

    memset(&s_depthBuffer, 0, sizeof(s_depthBuffer));
    s_depthBuffer.surface.use = GX2_SURFACE_USE_DEPTH_BUFFER;
    s_depthBuffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    s_depthBuffer.surface.width = WIIU_FB_WIDTH;
    s_depthBuffer.surface.height = WIIU_FB_HEIGHT;
    s_depthBuffer.surface.depth = 1;
    s_depthBuffer.surface.mipLevels = 1;
    s_depthBuffer.surface.format = GX2_SURFACE_FORMAT_FLOAT_R32;
    s_depthBuffer.surface.aa = GX2_AA_MODE1X;
    s_depthBuffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
    s_depthBuffer.viewNumSlices = 1;
    s_depthBuffer.depthClear = 1.0f;
    GX2CalcSurfaceSizeAndAlignment(&s_depthBuffer.surface);
    GX2InitDepthBufferRegs(&s_depthBuffer);
    s_depthBuffer.surface.image =
        wiiu_gfx_alloc_mem1(s_depthBuffer.surface.imageSize, s_depthBuffer.surface.alignment);

    if (s_colorBuffer.surface.image == NULL || s_depthBuffer.surface.image == NULL) {
        printf("[gx2] could not allocate the framebuffer in MEM1\n");
        s_status = GFX_RENDERING_FATAL;
        return false;
    }

    s_drawBuffer = memalign(GX2_VERTEX_BUFFER_ALIGNMENT, DRAW_BUFFER_SIZE);
    if (s_drawBuffer == NULL) {
        printf("[gx2] could not allocate the vertex ring\n");
        s_status = GFX_RENDERING_FATAL;
        return false;
    }
    s_drawOffset = 0;

    GX2SetColorBuffer(&s_colorBuffer, GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&s_depthBuffer);
    /* No depth clipping, like GL_DEPTH_CLAMP: the CPU clipper in the front
     * end already trims triangles to the frustum. */
    GX2SetRasterizerClipControl(TRUE, FALSE);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
    apply_blend_mode();
    s_status = GFX_RENDERING_READY;
    return true;
}

static void gfx_gx2_on_resize(void) {
}

static bool gfx_gx2_start_frame(void) {
    if (!wiiu_gfx_begin_frame()) {
        s_frameActive = false;
        return false;
    }
    s_frameActive = true;
    s_frameCount++;
    s_drawOffset = 0;

    GX2SetColorBuffer(&s_colorBuffer, GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&s_depthBuffer);
    GX2ClearColor(&s_colorBuffer, 0.0f, 0.0f, 0.0f, 1.0f);
    GX2ClearDepthStencilEx(&s_depthBuffer, s_depthBuffer.depthClear, s_depthBuffer.stencilClear,
                           GX2_CLEAR_FLAGS_BOTH);
    /* The clears above reset part of the GPU state. */
    wiiu_gfx_set_context_state();
    GX2SetColorBuffer(&s_colorBuffer, GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&s_depthBuffer);
    GX2SetRasterizerClipControl(TRUE, FALSE);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);

    s_viewport[0] = 0.0f;
    s_viewport[1] = 0.0f;
    s_viewport[2] = WIIU_FB_WIDTH;
    s_viewport[3] = WIIU_FB_HEIGHT;
    GX2SetViewport(0.0f, 0.0f, WIIU_FB_WIDTH, WIIU_FB_HEIGHT, 0.0f, 1.0f);
    GX2SetScissor(0, 0, WIIU_FB_WIDTH, WIIU_FB_HEIGHT);
    GX2SetDepthOnlyControl(s_depthTest, s_depthWrite, s_depthFunc);
    apply_polygon_offset();
    apply_blend_mode();
    if (s_currentShader != NULL && s_currentShader->valid) {
        gfx_gx2_load_shader(s_currentShader);
    }
    return true;
}

static bool gfx_gx2_begin_output_overlay(void) {
    /* Draw straight into the same framebuffer: there is no separate scene
     * target to resolve first. */
    return s_frameActive;
}

static void gfx_gx2_end_frame(void) {
    if (!s_frameActive) {
        return;
    }
    GX2CopyColorBufferToScanBuffer(&s_colorBuffer, GX2_SCAN_TARGET_TV);
    GX2CopyColorBufferToScanBuffer(&s_colorBuffer, GX2_SCAN_TARGET_DRC);
    s_frameActive = false;
}

static void gfx_gx2_finish_render(void) {
}

static void gfx_gx2_shutdown(void) {
    GX2DrawDone();
    free(s_drawBuffer);
    s_drawBuffer = NULL;
    wiiu_gfx_free_mem1(s_colorBuffer.surface.image);
    s_colorBuffer.surface.image = NULL;
    wiiu_gfx_free_mem1(s_depthBuffer.surface.image);
    s_depthBuffer.surface.image = NULL;
    s_status = GFX_RENDERING_UNINITIALIZED;
}

static enum GfxRenderingStatus gfx_gx2_get_status(void) {
    return s_status;
}

struct GfxRenderingAPI gfx_gx2_api = {
    .z_is_from_0_to_1 = gfx_gx2_z_is_from_0_to_1,
    .unload_shader = gfx_gx2_unload_shader,
    .load_shader = gfx_gx2_load_shader,
    .create_and_load_new_shader = gfx_gx2_create_and_load_new_shader,
    .lookup_shader = gfx_gx2_lookup_shader,
    .shader_get_info = gfx_gx2_shader_get_info,
    .new_texture = gfx_gx2_new_texture,
    .delete_texture = gfx_gx2_delete_texture,
    .select_texture = gfx_gx2_select_texture,
    .upload_texture = gfx_gx2_upload_texture,
    .set_sampler_parameters = gfx_gx2_set_sampler_parameters,
    .set_depth_mode = gfx_gx2_set_depth_mode,
    .set_viewport = gfx_gx2_set_viewport,
    .set_scissor = gfx_gx2_set_scissor,
    .set_shadow_view = gfx_gx2_set_shadow_view,
    .set_blend_mode = gfx_gx2_set_blend_mode,
    .set_modern_character_occluder = NULL,
    .draw_triangles = gfx_gx2_draw_triangles,
    .read_framebuffer_rgb = NULL,
    .get_modern_character_capture_dimensions = NULL,
    .get_modern_character_capture_projection = NULL,
    .get_modern_character_scene_projection = NULL,
    .read_modern_character_capture_rgba = NULL,
    .begin_modern_character_gpu_timing = NULL,
    .finish_modern_character_gpu_timing = NULL,
    .init = gfx_gx2_init,
    .on_resize = gfx_gx2_on_resize,
    .start_frame = gfx_gx2_start_frame,
    .begin_output_overlay = gfx_gx2_begin_output_overlay,
    .end_frame = gfx_gx2_end_frame,
    .finish_render = gfx_gx2_finish_render,
    .draw_modern_mesh = NULL,
    .modern_mesh_available = NULL,
    .draw_modern_skinned = NULL,
    .release_modern_asset = NULL,
    .clear_modern_mesh_cache = NULL,
    .upload_texture_mipped = gfx_gx2_upload_texture_mipped,
    .shutdown = gfx_gx2_shutdown,
    .get_status = gfx_gx2_get_status,
};

#endif /* __WIIU__ */
