/*
 * wiiu_main.c - the Wii U entry point.
 *
 * Does the few things a console app needs before the shared desktop main()
 * (platform/main_pc.c, here called mdkr64_wiiu_main) can run:
 *
 *  1. Starts ProcUI (the HOME menu / exit handling) via WHBProcInit.
 *  2. Creates the app's SD card folder and sends stdout/stderr to a log file
 *     in it, since there is no terminal.
 *  3. Finds the ROM. If it is missing, shows a text page saying where to put
 *     it and waits for the player to leave via HOME.
 *  4. Switches to an 8 MB stack. The Wii U gives the main thread a small
 *     stack, and the game was written for a PC where it gets megabytes;
 *     overflowing it doesn't always crash, it can silently corrupt memory.
 *     A "fiber" switch keeps the same thread and CPU core (ProcUI must stay
 *     on the core the app started on), only the stack changes.
 */
#ifdef __WIIU__

#include <errno.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <coreinit/debug.h>
#include <coreinit/fiber.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <whb/proc.h>

#include "wiiu_gfx.h"

#define GAME_STACK_SIZE (8 * 1024 * 1024)

int mdkr64_wiiu_main(int argc, char **argv);

static const char *const kRomNames[] = {
    "dkr.z64", "dkr.n64", "dkr.v64",
    "baserom.us.v80.z64", "baserom.z64",
};

static char s_romPath[512];
static int s_argc;
static char *s_argv[8];
static int s_result;

static void make_dirs(const char *path) {
    char buf[512];
    size_t len = strlen(path);
    if (len >= sizeof(buf)) {
        return;
    }
    memcpy(buf, path, len + 1);
    /* Skip the "fs:/vol/external01" device prefix: it always exists. */
    for (size_t i = strlen("fs:/vol/external01/"); i < len; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            mkdir(buf, 0777);
            buf[i] = '/';
        }
    }
    mkdir(buf, 0777);
}

static int find_rom(void) {
    for (size_t i = 0; i < sizeof(kRomNames) / sizeof(kRomNames[0]); i++) {
        struct stat st;
        snprintf(s_romPath, sizeof(s_romPath), "%s/%s", MDKR_WIIU_APP_DIR, kRomNames[i]);
        if (stat(s_romPath, &st) == 0 && S_ISREG(st.st_mode)) {
            return 1;
        }
    }
    return 0;
}

/* A plain text page using the OS's own framebuffer console, for problems found
 * before the game (and GX2) start. Stays up until the player presses HOME and
 * chooses to close the app. */
static void show_message_page(const char *const *lines, int count) {
    WHBLogConsoleInit();
    for (int i = 0; i < count; i++) {
        WHBLogPrint(lines[i]);
    }
    while (WHBProcIsRunning()) {
        WHBLogConsoleDraw();
        OSSleepTicks(OSMillisecondsToTicks(100));
    }
    WHBLogConsoleFree();
}

static void game_entry(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    s_result = mdkr64_wiiu_main(s_argc, s_argv);
}

int main(int argc, char **argv) {
    uint8_t *stack;
    (void)argc;
    (void)argv;

    WHBProcInit();

    make_dirs(MDKR_WIIU_APP_DIR);
    chdir(MDKR_WIIU_APP_DIR);
    freopen(MDKR_WIIU_APP_DIR "/dkr.log", "w", stdout);
    freopen(MDKR_WIIU_APP_DIR "/dkr.log", "a", stderr);
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("[wiiu] Diddy Kong Racing for Wii U starting\n");

    if (!find_rom()) {
        static const char *const lines[] = {
            "Diddy Kong Racing - Wii U port",
            "",
            "The game ROM was not found.",
            "",
            "Copy your Diddy Kong Racing ROM (US 1.1 or EU 1.1)",
            "to the SD card as:",
            "",
            "    sd:/wiiu/apps/dkr/dkr.z64",
            "",
            "(.z64, .v64 and .n64 byte orders are all accepted.)",
            "",
            "Press HOME to return to the Wii U Menu.",
        };
        printf("[wiiu] no ROM found in %s\n", MDKR_WIIU_APP_DIR);
        show_message_page(lines, (int)(sizeof(lines) / sizeof(lines[0])));
        WHBProcShutdown();
        return 0;
    }
    printf("[wiiu] ROM: %s\n", s_romPath);

    s_argc = 0;
    s_argv[s_argc++] = (char *)"mdkr64";
    s_argv[s_argc++] = (char *)"--rom";
    s_argv[s_argc++] = s_romPath;
    s_argv[s_argc] = NULL;

    stack = memalign(64, GAME_STACK_SIZE);
    if (stack == NULL) {
        OSFatal("Diddy Kong Racing: could not allocate the game's stack.");
    }
    /* Stacks grow down: pass the top end, less room for the first frame. */
    OSSwitchFiberEx(0, 0, 0, 0, game_entry, stack + GAME_STACK_SIZE - 64);

    printf("[wiiu] game returned %d\n", s_result);
    fflush(stdout);
    wiiu_gfx_shutdown();
    if (s_result != 0) {
        static const char *const lines[] = {
            "Diddy Kong Racing - Wii U port",
            "",
            "The game stopped because of an error.",
            "Details are in sd:/wiiu/apps/dkr/dkr.log",
            "",
            "Press HOME to return to the Wii U Menu.",
        };
        show_message_page(lines, (int)(sizeof(lines) / sizeof(lines[0])));
    } else {
        /* Keep ProcUI happy until the system has actually let us go. */
        while (WHBProcIsRunning()) {
            OSSleepTicks(OSMillisecondsToTicks(16));
        }
    }
    WHBProcShutdown();
    return 0;
}

#endif /* __WIIU__ */
