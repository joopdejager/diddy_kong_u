/*
 * wiiu_gfx.c - the Wii U screen: GX2 setup, scan buffers and the HOME menu.
 * See wiiu_gfx.h. Based on libultraship's gfx_wiiu.cpp (MIT, GaryOderNichts).
 */
#ifdef __WIIU__

#include "wiiu_gfx.h"

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <coreinit/memdefaultheap.h>
#include <coreinit/memexpheap.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/memheap.h>
#include <coreinit/memory.h>
#include <coreinit/time.h>
#include <gx2/context.h>
#include <gx2/display.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2/swap.h>
#include <proc_ui/procui.h>
#include <whb/proc.h>

#define COMMAND_BUFFER_POOL_SIZE 0x400000

static MEMHeapHandle s_heapMem1;
static MEMHeapHandle s_heapForeground;
static bool s_hasForeground;
static bool s_initialized;
static void *s_mem1Storage;
static void *s_commandBufferPool;
static GX2ContextState *s_contextState;

static GX2TVRenderMode s_tvRenderMode;
static void *s_tvScanBuffer;
static uint32_t s_tvScanBufferSize;

static GX2DrcRenderMode s_drcRenderMode;
static void *s_drcScanBuffer;
static uint32_t s_drcScanBufferSize;

static bool init_frame_heap(MEMBaseHeapType type, MEMHeapHandle *out) {
    MEMHeapHandle heap = MEMGetBaseHeapHandle(type);
    uint32_t size = MEMGetAllocatableSizeForFrmHeapEx(heap, 4);
    void *base;
    if (size == 0) {
        return false;
    }
    base = MEMAllocFromFrmHeapEx(heap, size, 4);
    if (base == NULL) {
        return false;
    }
    *out = MEMCreateExpHeapEx(base, size, 0);
    return *out != NULL;
}

static void destroy_frame_heap(MEMBaseHeapType type, MEMHeapHandle *handle) {
    if (*handle != NULL) {
        MEMDestroyExpHeap(*handle);
        *handle = NULL;
    }
    MEMFreeToFrmHeap(MEMGetBaseHeapHandle(type), MEM_FRM_HEAP_FREE_ALL);
}

void *wiiu_gfx_alloc_mem1(uint32_t size, uint32_t alignment) {
    if (s_heapMem1 == NULL) {
        return NULL;
    }
    return MEMAllocFromExpHeapEx(s_heapMem1, size, alignment < 4 ? 4 : alignment);
}

void wiiu_gfx_free_mem1(void *block) {
    if (s_heapMem1 != NULL && block != NULL) {
        MEMFreeToExpHeap(s_heapMem1, block);
    }
}

static void *alloc_foreground(uint32_t size, uint32_t alignment) {
    if (s_heapForeground == NULL) {
        return NULL;
    }
    return MEMAllocFromExpHeapEx(s_heapForeground, size, alignment < 4 ? 4 : alignment);
}

/* ProcUI gave us the screen back (or gave it to us the first time). The
 * scan buffers live in foreground memory, which only exists while we have it. */
static uint32_t proc_acquired(void *context) {
    (void)context;
    s_hasForeground = true;
    if (!init_frame_heap(MEM_BASE_HEAP_FG, &s_heapForeground)) {
        printf("[wiiu] foreground heap unavailable\n");
        return 0;
    }
    s_tvScanBuffer = alloc_foreground(s_tvScanBufferSize, GX2_SCAN_BUFFER_ALIGNMENT);
    if (s_tvScanBuffer != NULL) {
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, s_tvScanBuffer, s_tvScanBufferSize);
        GX2SetTVBuffer(s_tvScanBuffer, s_tvScanBufferSize, s_tvRenderMode,
                       GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE);
    }
    s_drcScanBuffer = alloc_foreground(s_drcScanBufferSize, GX2_SCAN_BUFFER_ALIGNMENT);
    if (s_drcScanBuffer != NULL) {
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, s_drcScanBuffer, s_drcScanBufferSize);
        GX2SetDRCBuffer(s_drcScanBuffer, s_drcScanBufferSize, s_drcRenderMode,
                        GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE);
    }
    return 0;
}

static uint32_t proc_released(void *context) {
    (void)context;
    if (s_initialized) {
        GX2DrawDone();
    }
    s_tvScanBuffer = NULL;
    s_drcScanBuffer = NULL;
    destroy_frame_heap(MEM_BASE_HEAP_FG, &s_heapForeground);
    s_hasForeground = false;
    return 0;
}

bool wiiu_gfx_init(void) {
    uint32_t mem1Addr = 0;
    uint32_t mem1Size = 0;
    uint32_t unused = 0;

    if (s_initialized) {
        return true;
    }

    /* ProcUI needs somewhere to save MEM1 while the HOME menu borrows it. */
    OSGetMemBound(OS_MEM1, &mem1Addr, &mem1Size);
    s_mem1Storage = memalign(0x40, mem1Size);
    if (s_mem1Storage == NULL) {
        printf("[wiiu] could not allocate MEM1 save area\n");
        return false;
    }
    ProcUISetMEM1Storage(s_mem1Storage, mem1Size);

    if (!init_frame_heap(MEM_BASE_HEAP_MEM1, &s_heapMem1)) {
        printf("[wiiu] MEM1 heap unavailable\n");
        return false;
    }

    s_commandBufferPool = memalign(GX2_COMMAND_BUFFER_ALIGNMENT, COMMAND_BUFFER_POOL_SIZE);
    if (s_commandBufferPool == NULL) {
        return false;
    }
    {
        uint32_t attribs[] = {
            GX2_INIT_CMD_BUF_BASE, (uintptr_t)s_commandBufferPool,
            GX2_INIT_CMD_BUF_POOL_SIZE, COMMAND_BUFFER_POOL_SIZE,
            GX2_INIT_ARGC, 0,
            GX2_INIT_ARGV, 0,
            GX2_INIT_END,
        };
        GX2Init(attribs);
    }

    switch (GX2GetSystemTVScanMode()) {
        case GX2_TV_SCAN_MODE_480I:
        case GX2_TV_SCAN_MODE_480P:
            s_tvRenderMode = GX2_TV_RENDER_MODE_WIDE_480P;
            break;
        case GX2_TV_SCAN_MODE_1080I:
        case GX2_TV_SCAN_MODE_1080P:
            s_tvRenderMode = GX2_TV_RENDER_MODE_WIDE_1080P;
            break;
        case GX2_TV_SCAN_MODE_720P:
        default:
            s_tvRenderMode = GX2_TV_RENDER_MODE_WIDE_720P;
            break;
    }
    s_drcRenderMode = GX2GetSystemDRCScanMode();

    GX2CalcTVSize(s_tvRenderMode, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
                  GX2_BUFFERING_MODE_DOUBLE, &s_tvScanBufferSize, &unused);
    GX2CalcDRCSize(s_drcRenderMode, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
                   GX2_BUFFERING_MODE_DOUBLE, &s_drcScanBufferSize, &unused);

    ProcUIRegisterCallback(PROCUI_CALLBACK_ACQUIRE, proc_acquired, NULL, 100);
    ProcUIRegisterCallback(PROCUI_CALLBACK_RELEASE, proc_released, NULL, 100);
    proc_acquired(NULL);

    s_contextState = memalign(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState));
    if (s_contextState == NULL) {
        return false;
    }
    GX2SetupContextStateEx(s_contextState, TRUE);
    GX2SetContextState(s_contextState);

    GX2SetTVScale(WIIU_FB_WIDTH, WIIU_FB_HEIGHT);
    GX2SetDRCScale(WIIU_FB_WIDTH, WIIU_FB_HEIGHT);
    GX2SetSwapInterval(1);

    s_initialized = true;
    printf("[wiiu] GX2 ready, rendering at %dx%d\n", WIIU_FB_WIDTH, WIIU_FB_HEIGHT);
    return true;
}

void wiiu_gfx_shutdown(void) {
    if (!s_initialized) {
        return;
    }
    if (s_hasForeground) {
        proc_released(NULL);
        destroy_frame_heap(MEM_BASE_HEAP_MEM1, &s_heapMem1);
    }
    GX2Shutdown();
    free(s_contextState);
    s_contextState = NULL;
    free(s_commandBufferPool);
    s_commandBufferPool = NULL;
    ProcUISetMEM1Storage(NULL, 0);
    free(s_mem1Storage);
    s_mem1Storage = NULL;
    s_initialized = false;
}

bool wiiu_gfx_has_foreground(void) {
    return s_hasForeground;
}

bool wiiu_gfx_process(void) {
    /* WHBProcIsRunning runs ProcUIProcessMessages, which calls the callbacks
     * above and blocks while we are in the background. */
    return WHBProcIsRunning();
}

void wiiu_gfx_set_context_state(void) {
    if (s_contextState != NULL) {
        GX2SetContextState(s_contextState);
    }
}

bool wiiu_gfx_begin_frame(void) {
    uint32_t swapCount = 0;
    uint32_t flipCount = 0;
    OSTime lastFlip = 0;
    OSTime lastVsync = 0;
    int waits = 0;

    if (!s_hasForeground) {
        return false;
    }
    for (;;) {
        GX2GetSwapStatus(&swapCount, &flipCount, &lastFlip, &lastVsync);
        if (flipCount >= swapCount) {
            return true;
        }
        if (waits++ >= 10) {
            return true; /* GPU is late: draw anyway rather than stall forever */
        }
        GX2WaitForVsync();
    }
}

void wiiu_gfx_present(void) {
    static bool enabled;
    if (!s_hasForeground) {
        return;
    }
    GX2SwapScanBuffers();
    GX2Flush();
    wiiu_gfx_set_context_state();
    if (!enabled) {
        /* Only turn the outputs on once there is a real frame to show, so the
         * first thing on screen isn't uninitialised memory. */
        GX2SetTVEnable(TRUE);
        GX2SetDRCEnable(TRUE);
        enabled = true;
    }
}

#endif /* __WIIU__ */
