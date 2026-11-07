/*
 * gx2_vertex_map.h - how a vertex packed by the renderer front end maps onto
 * the GX2 shaders' fixed attribute slots.
 *
 * The front end (gfx_pc_dkr.c) packs each vertex as tightly as the GL back
 * end's shader expects, in this order:
 *   position (4) | coverage diagnostics (6, rare) | world position (3) |
 *   smooth normal + light dir (6) | per texture: s t (2), then for S and
 *   for T: clamp (1) and mask (1) if enabled | fog (4) | each combiner
 *   input (4 with alpha, 3 without)
 * The generated GX2 shaders (gx2_shader_gen.c) instead read four floats per
 * slot: position, tex0 (s t clampS clampT), tex1, fog, then each input.
 * Absent features take no slot. This header works out the copy runs once
 * per shader; gfx_gx2.c applies them to every vertex. The big-endian test
 * build checks src_floats against what the front end actually sends.
 */
#ifndef GX2_VERTEX_MAP_H
#define GX2_VERTEX_MAP_H

#include <stdint.h>

#include "gfx_cc.h"

struct Gx2CopyRun {
    uint8_t src;   /* offset in the source vertex, in floats */
    uint8_t dst;   /* offset in the repacked vertex, in floats */
    uint8_t count; /* floats to copy */
};

struct Gx2VertexMap {
    uint32_t src_floats; /* floats per vertex as packed by the front end */
    uint32_t dst_floats; /* floats per vertex as read by the shader */
    uint8_t num_runs;
    struct Gx2CopyRun runs[24];
    /* Slots set to 1.0 because the source has no value for them (the alpha
     * of a 3-component combiner input). Everything else absent is 0. */
    uint8_t num_ones;
    uint8_t ones[8];
};

static inline void gx2_vertex_map_add(struct Gx2VertexMap *m, uint32_t src, uint32_t dst,
                                      uint32_t count) {
    if (count == 0) {
        return;
    }
    if (m->num_runs > 0) {
        struct Gx2CopyRun *last = &m->runs[m->num_runs - 1];
        if (last->src + last->count == src && last->dst + last->count == dst) {
            last->count = (uint8_t)(last->count + count);
            return;
        }
    }
    if (m->num_runs < sizeof(m->runs) / sizeof(m->runs[0])) {
        m->runs[m->num_runs].src = (uint8_t)src;
        m->runs[m->num_runs].dst = (uint8_t)dst;
        m->runs[m->num_runs].count = (uint8_t)count;
        m->num_runs++;
    }
}

static inline void gx2_vertex_map_build(struct Gx2VertexMap *m, const struct CCFeatures *cc) {
    uint32_t src = 0;
    uint32_t dst = 0;

    m->num_runs = 0;
    m->num_ones = 0;

    gx2_vertex_map_add(m, src, dst, 4); /* position */
    src += 4;
    dst += 4;

    if (cc->opt_alpha && cc->diag_rdp_cvg_memory_blend) {
        src += 6;
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
        gx2_vertex_map_add(m, src, dst, 2); /* s, t */
        src += 2;
        for (int axis = 0; axis < 2; axis++) {
            if (cc->clamp[i][axis]) {
                gx2_vertex_map_add(m, src, dst + 2 + (uint32_t)axis, 1);
                src += 1;
            }
            if (cc->tile_mask[i][axis]) {
                src += 1; /* shader-side mask wrap is not done on GX2 */
            }
        }
        dst += 4;
    }
    if (cc->opt_fog) {
        gx2_vertex_map_add(m, src, dst, 4);
        src += 4;
        dst += 4;
    }
    for (int i = 0; i < cc->num_inputs; i++) {
        uint32_t n = cc->opt_alpha ? 4u : 3u;
        gx2_vertex_map_add(m, src, dst, n);
        if (n == 3 && m->num_ones < sizeof(m->ones)) {
            m->ones[m->num_ones++] = (uint8_t)(dst + 3);
        }
        src += n;
        dst += 4;
    }
    m->src_floats = src;
    m->dst_floats = dst;
}

#endif
