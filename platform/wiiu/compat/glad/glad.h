/*
 * Stand-in for glad's OpenGL loader header on the Wii U.
 *
 * platform/platform_sdl_min.c is shared with the desktop build and contains
 * the OpenGL window/fence code. The Wii U has no OpenGL; that code is never
 * reached there (the GL context is never created, so s_glReady stays 0), but
 * it still has to compile. This header supplies just the types, constants and
 * functions it names, as inert stubs. Rendering goes through GX2 instead
 * (platform/wiiu/gfx_gx2.c).
 */
#ifndef MDKR_WIIU_GLAD_STUB_H
#define MDKR_WIIU_GLAD_STUB_H

#include <stdint.h>

typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned char GLubyte;
typedef uint64_t GLuint64;
typedef struct __GLsync *GLsync;

#define GL_ALREADY_SIGNALED 0x911A
#define GL_TIMEOUT_EXPIRED 0x911B
#define GL_CONDITION_SATISFIED 0x911C
#define GL_WAIT_FAILED 0x911D
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#define GL_BACK 0x0405
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02

static inline GLsync glFenceSync(GLenum condition, GLbitfield flags) {
    (void)condition; (void)flags; return (GLsync)0;
}
static inline GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    (void)sync; (void)flags; (void)timeout; return GL_ALREADY_SIGNALED;
}
static inline void glDeleteSync(GLsync sync) { (void)sync; }
static inline void glFinish(void) {}
static inline void glFlush(void) {}
static inline const GLubyte *glGetString(GLenum name) {
    (void)name; return (const GLubyte *)"GX2";
}
static inline int gladLoadGLLoader(void *loader) { (void)loader; return 0; }
typedef void *(*GLADloadproc)(const char *name);

#endif
