/*
 * gfx_opengl_stubs.c - two questions the renderer front end and the SDL layer
 * ask the OpenGL back end directly. There is no GL back end on the Wii U (or
 * in its big-endian test build), so these give the "nothing to report"
 * answers.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __WIIU__
#include "wiiu_gfx.h"
#endif

int gfx_opengl_max_offscreen_dim(void) {
#ifdef __WIIU__
    /* The renderer must never size its scene above the GX2 framebuffer. */
    return WIIU_FB_WIDTH;
#else
    return 4096;
#endif
}

bool gfx_opengl_copy_captured_frame(int width, int height, uint8_t *rgb_out) {
    (void)width;
    (void)height;
    (void)rgb_out;
    return false;
}

