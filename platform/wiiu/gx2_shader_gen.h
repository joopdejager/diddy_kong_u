/*  gx2_shader_gen.h - Fast3D GX2 shader generator for libultraship (MIT, see third_party/libultraship_wiiu/LICENSE)

    Created in 2022 by GaryOderNichts
*/
#pragma once

#include "gfx_cc.h"
#include <gx2/shaders.h>

#define SHADER_FIRST_TEXTURE 0
#define SHADER_FIRST_MASK_TEXTURE 2
#define SHADER_FIRST_REPLACEMENT_TEXTURE 4

#ifdef __cplusplus
extern "C" {
#endif

struct ShaderGroup {
    GX2VertexShader vertexShader;
    GX2PixelShader pixelShader;
    GX2FetchShader fetchShader;

    uint32_t stride;

    uint32_t numAttributes;
    GX2AttribStream attributes[13];
};

int gx2GenerateShaderGroup(struct ShaderGroup* group, struct CCFeatures* cc_features);

void gx2FreeShaderGroup(struct ShaderGroup* group);

#ifdef __cplusplus
}
#endif
