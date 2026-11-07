/*
 * Wii U stand-in for platform/fast3d/gfx_character_text.c.
 *
 * The desktop file shapes custom-character names with HarfBuzz and SheenBidi
 * (C++ libraries the Wii U build leaves out). Custom characters are a desktop
 * feature, so here every call reports "not available" and the game keeps its
 * retail font, which is the same fallback the desktop uses for text it cannot
 * shape.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gfx_character_text.h"

bool gfx_character_text_measure(const char *source, size_t source_capacity,
                                uint32_t max_width, uint32_t height,
                                GfxCharacterTextMetrics *metrics) {
    (void)source; (void)source_capacity; (void)max_width; (void)height;
    if (metrics != NULL) {
        memset(metrics, 0, sizeof(*metrics));
    }
    return false;
}

bool gfx_character_text_render_rgba(const char *source, size_t source_capacity,
                                    uint32_t max_width, uint32_t height, uint8_t *output,
                                    size_t output_size, size_t output_stride,
                                    GfxCharacterTextMetrics *metrics) {
    (void)source; (void)source_capacity; (void)max_width; (void)height;
    (void)output; (void)output_size; (void)output_stride;
    if (metrics != NULL) {
        memset(metrics, 0, sizeof(*metrics));
    }
    return false;
}

const char *gfx_character_text_fallback_reason_name(GfxCharacterTextFallbackReason reason) {
    (void)reason;
    return "unavailable-on-wiiu";
}

const char *gfx_character_text_latin_face_base85(void) { return ""; }
const char *gfx_character_text_arabic_face_base85(void) { return ""; }
const char *gfx_character_text_hebrew_face_base85(void) { return ""; }
void gfx_character_text_shutdown(void) {}
