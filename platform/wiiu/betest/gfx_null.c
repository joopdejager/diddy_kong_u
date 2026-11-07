/*
 * gfx_null.c - a rendering back end that draws nothing, for the big-endian
 * test build (see docs/wiiu/big-endian-test.md).
 *
 * The test build runs the whole game, including the renderer front end that
 * interprets the game's display lists, on 32-bit big-endian PowerPC Linux
 * under qemu. Only the final GPU step is replaced by this file, which counts
 * what it was asked to do and prints a summary now and then, so a run shows
 * whether the game reached its menus and drew real scenes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gx2_vertex_map.h"

struct ShaderProgram {
    uint64_t id0;
    uint32_t id1;
    uint8_t num_inputs;
    bool used_textures[2];
    struct Gx2VertexMap map;
    struct ShaderProgram *next;
};

static struct ShaderProgram *s_shaders;
static struct ShaderProgram *s_current;
static uint64_t s_layoutMismatches;
static uint64_t s_layoutChecks;
static uint32_t s_textures;
static uint64_t s_frameTris;
static uint64_t s_frameDraws;
static uint32_t s_frames;
static uint32_t s_shaderCount;
static uint64_t s_texelsUploaded;

static bool null_z01(void) { return false; }
static void null_unload(struct ShaderProgram *p) { (void)p; s_current = NULL; }
static void null_load(struct ShaderProgram *p) { s_current = p; }

static struct ShaderProgram *null_lookup(uint64_t id0, uint32_t id1) {
    for (struct ShaderProgram *p = s_shaders; p != NULL; p = p->next) {
        if (p->id0 == id0 && p->id1 == id1) {
            return p;
        }
    }
    return NULL;
}

static struct ShaderProgram *null_create(uint64_t id0, uint32_t id1) {
    struct CCFeatures cc;
    struct ShaderProgram *p = calloc(1, sizeof(*p));
    gfx_cc_get_features(id0, id1, &cc);
    p->id0 = id0;
    p->id1 = id1;
    p->num_inputs = (uint8_t)cc.num_inputs;
    p->used_textures[0] = cc.used_textures[0];
    p->used_textures[1] = cc.used_textures[1];
    gx2_vertex_map_build(&p->map, &cc);
    p->next = s_shaders;
    s_shaders = p;
    s_current = p;
    s_shaderCount++;
    printf("[gfx-null] new shader %016llx:%08x\n", (unsigned long long)id0, id1);
    return p;
}

static void null_info(struct ShaderProgram *p, uint8_t *n, bool used[2]) {
    *n = p->num_inputs;
    used[0] = p->used_textures[0];
    used[1] = p->used_textures[1];
}

static uint32_t null_new_texture(void) { return ++s_textures; }
static void null_delete_texture(uint32_t id) { (void)id; }
static void null_select_texture(int tile, uint32_t id) { (void)tile; (void)id; }
static bool null_upload(const uint8_t *buf, int w, int h) {
    (void)buf;
    s_texelsUploaded += (uint64_t)w * (uint64_t)h;
    return true;
}
static void null_sampler(int s, bool l, uint32_t a, uint32_t b) { (void)s; (void)l; (void)a; (void)b; }
static void null_depth(bool a, bool b, bool c, bool d, uint16_t z) { (void)a; (void)b; (void)c; (void)d; (void)z; }
static void null_viewport(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
static void null_scissor(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
static void null_shadow_view(int v) { (void)v; }
static void null_blend(enum GfxBlendMode m) { (void)m; }
static void null_draw(float buf[], size_t len, size_t tris) {
    (void)buf;
    /* The check the GX2 back end depends on: its idea of the vertex size
     * must match what the front end really packed. */
    if (s_current != NULL && tris > 0) {
        s_layoutChecks++;
        if (len != tris * 3u * s_current->map.src_floats) {
            if (s_layoutMismatches++ < 10) {
                printf("[gfx-null] LAYOUT MISMATCH shader %016llx:%08x: %zu floats for %zu "
                       "tris, map expects %u per vertex\n",
                       (unsigned long long)s_current->id0, s_current->id1, len, tris,
                       s_current->map.src_floats);
            }
        }
    }
    s_frameTris += tris;
    s_frameDraws++;
}
static bool null_init(void) { return true; }
static void null_resize(void) {}
static bool null_start(void) {
    s_frameTris = 0;
    s_frameDraws = 0;
    return true;
}
static bool null_overlay(void) { return true; }
static void null_end(void) {
    s_frames++;
    if (s_frames <= 5 || s_frames % 60 == 0) {
        printf("[gfx-null] frame %u: %llu draws, %llu triangles, %u shaders, %u textures, "
               "%llu texels uploaded, layout %llu/%llu bad\n",
               s_frames, (unsigned long long)s_frameDraws, (unsigned long long)s_frameTris,
               s_shaderCount, s_textures, (unsigned long long)s_texelsUploaded,
               (unsigned long long)s_layoutMismatches, (unsigned long long)s_layoutChecks);
        fflush(stdout);
    }
}
static void null_finish(void) {}
static void null_shutdown(void) {}
static enum GfxRenderingStatus null_status(void) { return GFX_RENDERING_READY; }

struct GfxRenderingAPI gfx_null_api = {
    .z_is_from_0_to_1 = null_z01,
    .unload_shader = null_unload,
    .load_shader = null_load,
    .create_and_load_new_shader = null_create,
    .lookup_shader = null_lookup,
    .shader_get_info = null_info,
    .new_texture = null_new_texture,
    .delete_texture = null_delete_texture,
    .select_texture = null_select_texture,
    .upload_texture = null_upload,
    .set_sampler_parameters = null_sampler,
    .set_depth_mode = null_depth,
    .set_viewport = null_viewport,
    .set_scissor = null_scissor,
    .set_shadow_view = null_shadow_view,
    .set_blend_mode = null_blend,
    .draw_triangles = null_draw,
    .init = null_init,
    .on_resize = null_resize,
    .start_frame = null_start,
    .begin_output_overlay = null_overlay,
    .end_frame = null_end,
    .finish_render = null_finish,
    .shutdown = null_shutdown,
    .get_status = null_status,
};
