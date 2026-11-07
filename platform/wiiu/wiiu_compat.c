/*
 * wiiu_compat.c - small gaps between wut's C library and what the shared
 * desktop code links against.
 */
#ifdef __WIIU__

#include <errno.h>
#include <malloc.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* newlib on the Wii U declares but does not implement posix_memalign. */
int posix_memalign(void **memptr, size_t alignment, size_t size) {
    void *p;
    if (memptr == NULL || alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0) {
        return EINVAL;
    }
    p = memalign(alignment, size);
    if (p == NULL) {
        return ENOMEM;
    }
    *memptr = p;
    return 0;
}

/* Advisory file locks keep two desktop copies of the game from writing the
 * same save at once. Only one app runs at a time on the Wii U. */
int flock(int fd, int operation) {
    (void)fd;
    (void)operation;
    return 0;
}

/* The renderer front end asks the GL back end two questions directly. The
 * Wii U uses GX2 (gfx_gx2.c), so give the answers that mean "no limit beyond
 * the framebuffer" and "no captured frame". */
int gfx_opengl_max_offscreen_dim(void) {
    return 4096;
}

bool gfx_opengl_copy_captured_frame(int width, int height, uint8_t *rgb_out) {
    (void)width;
    (void)height;
    (void)rgb_out;
    return false;
}

#endif
