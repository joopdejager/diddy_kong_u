/*
 * wiiu_gfx.h - the Wii U screen: GX2 setup, scan buffers and the HOME menu.
 *
 * On a PC, SDL owns the window and OpenGL draws into it. On the Wii U there is
 * no OpenGL, so this module sets up GX2 (the console's graphics API) itself,
 * owns the TV and GamePad scan buffers, and handles ProcUI, the system layer
 * that runs the HOME menu and tells the app when it must give up the screen.
 *
 * Based on libultraship's Wii U window manager (gfx_wiiu.cpp, MIT, by
 * GaryOderNichts), rewritten in C for this port.
 */
#ifndef WIIU_GFX_H
#define WIIU_GFX_H

#include <stdbool.h>
#include <stdint.h>

/* The size the game is rendered at. The result is scaled to the TV's mode
 * and to the GamePad's 854x480 screen when it is copied out. */
#define WIIU_FB_WIDTH 1280
#define WIIU_FB_HEIGHT 720

bool wiiu_gfx_init(void);
void wiiu_gfx_shutdown(void);

/* True while the app owns the screen (not in the HOME menu or exiting). */
bool wiiu_gfx_has_foreground(void);

/* Runs ProcUI. Returns false once the system has asked the app to exit.
 * While the HOME menu is open this blocks until the app gets the screen back. */
bool wiiu_gfx_process(void);

/* Waits until the GPU has caught up with the previous frame. */
bool wiiu_gfx_begin_frame(void);
/* Puts the finished frame on screen and restores the GX2 context state. */
void wiiu_gfx_present(void);
void wiiu_gfx_set_context_state(void);

/* MEM1 is the Wii U's 32 MB of fast graphics memory. ProcUI takes it away
 * while the HOME menu is open, so only per-session buffers live there. */
void *wiiu_gfx_alloc_mem1(uint32_t size, uint32_t alignment);
void wiiu_gfx_free_mem1(void *block);

#endif
