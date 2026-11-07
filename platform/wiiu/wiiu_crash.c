/*
 * wiiu_crash.c - report crashes instead of freezing.
 *
 * Without this a crash (a bad memory access or a bad instruction) can leave
 * the console stuck on the last frame. The Wii U lets an app handle these
 * itself: we write where it happened to dkr.log and show it on screen with
 * OSFatal, which also lets the player power off cleanly.
 *
 * The addresses are code addresses in mdkr64.rpx. To turn them into
 * function names and lines, see "Reading a crash" in docs/wiiu/wiiu-build.md.
 * "main is at" is printed so a different load address can be corrected for.
 */
#ifdef __WIIU__

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <coreinit/context.h>
#include <coreinit/debug.h>
#include <coreinit/exception.h>

int main(int argc, char **argv);

static char s_report[1024];

static void format_report(const char *what, const OSContext *ctx) {
    size_t used;
    uint32_t sp = ctx->gpr[1];
    int depth;

    used = (size_t)snprintf(s_report, sizeof(s_report),
                            "Diddy Kong Racing crashed (%s).\n"
                            "PC %08lX  LR %08lX  DAR %08lX\n"
                            "main is at %08lX\n"
                            "Callers:",
                            what, (unsigned long)ctx->srr0, (unsigned long)ctx->lr,
                            (unsigned long)ctx->dar, (unsigned long)(uintptr_t)main);
    /* PowerPC stack frames: [sp] is the caller's sp, [sp+4] its saved LR. */
    for (depth = 0; depth < 12 && used < sizeof(s_report) - 16; depth++) {
        uint32_t next;
        uint32_t lr;
        if (sp == 0 || (sp & 3) != 0 || sp < 0x10000000u || sp >= 0x50000000u) {
            break;
        }
        next = *(const uint32_t *)(uintptr_t)sp;
        if (next <= sp) {
            break;
        }
        lr = *(const uint32_t *)(uintptr_t)(next + 4);
        used += (size_t)snprintf(s_report + used, sizeof(s_report) - used, " %08lX",
                                 (unsigned long)lr);
        sp = next;
    }
    snprintf(s_report + used, sizeof(s_report) - used,
             "\nDetails are in sd:/wiiu/apps/dkr/dkr.log\n");
}

static BOOL on_exception(const char *what, OSContext *ctx) {
    format_report(what, ctx);
    fputs("\n[CRASH] ", stderr);
    fputs(s_report, stderr);
    fflush(stderr);
    fflush(stdout);
    OSFatal(s_report);
    return FALSE;
}

static BOOL on_dsi(OSContext *ctx) { return on_exception("bad memory access", ctx); }
static BOOL on_isi(OSContext *ctx) { return on_exception("bad instruction address", ctx); }
static BOOL on_program(OSContext *ctx) { return on_exception("illegal instruction", ctx); }

void wiiu_crash_install(void) {
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_DSI, on_dsi);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_ISI, on_isi);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_PROGRAM,
                             on_program);
}

#endif /* __WIIU__ */
