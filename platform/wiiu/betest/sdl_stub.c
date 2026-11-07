/*
 * sdl_stub.c - just enough of SDL2 for the big-endian test build.
 *
 * There are no PowerPC builds of SDL2 to install on an x86 Ubuntu machine,
 * and the test needs no window, sound or controllers anyway. So the test
 * program links against this file instead: threads, locks, condition
 * variables and clocks are real (pthreads / clock_gettime), because the game
 * relies on them; everything else reports "nothing there" (no events, no
 * controllers, no audio device).
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL.h>
#include <SDL_syswm.h>

/* ---- time --------------------------------------------------------------- */
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static uint64_t s_startNs;
Uint32 SDL_GetTicks(void) {
    if (s_startNs == 0) s_startNs = now_ns();
    return (Uint32)((now_ns() - s_startNs) / 1000000ull);
}
Uint64 SDL_GetPerformanceCounter(void) { return now_ns(); }
Uint64 SDL_GetPerformanceFrequency(void) { return 1000000000ull; }

/* ---- threads ------------------------------------------------------------ */
struct SDL_Thread {
    pthread_t thread;
    SDL_ThreadFunction fn;
    void *data;
    int status;
};
static void *thread_main(void *arg) {
    struct SDL_Thread *t = arg;
    t->status = t->fn(t->data);
    return NULL;
}
SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data) {
    struct SDL_Thread *t = calloc(1, sizeof(*t));
    (void)name;
    if (t == NULL) return NULL;
    t->fn = fn;
    t->data = data;
    if (pthread_create(&t->thread, NULL, thread_main, t) != 0) {
        free(t);
        return NULL;
    }
    return t;
}
void SDL_WaitThread(SDL_Thread *t, int *status) {
    if (t == NULL) return;
    pthread_join(t->thread, NULL);
    if (status) *status = t->status;
    free(t);
}

struct SDL_mutex { pthread_mutex_t m; };
SDL_mutex *SDL_CreateMutex(void) {
    struct SDL_mutex *m = calloc(1, sizeof(*m));
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m->m, &attr);
    return m;
}
void SDL_DestroyMutex(SDL_mutex *m) { if (m) { pthread_mutex_destroy(&m->m); free(m); } }
int SDL_LockMutex(SDL_mutex *m) { return m ? pthread_mutex_lock(&m->m) : 0; }
int SDL_UnlockMutex(SDL_mutex *m) { return m ? pthread_mutex_unlock(&m->m) : 0; }

struct SDL_cond { pthread_cond_t c; };
SDL_cond *SDL_CreateCond(void) {
    struct SDL_cond *c = calloc(1, sizeof(*c));
    pthread_cond_init(&c->c, NULL);
    return c;
}
void SDL_DestroyCond(SDL_cond *c) { if (c) { pthread_cond_destroy(&c->c); free(c); } }
int SDL_CondSignal(SDL_cond *c) { return c ? pthread_cond_signal(&c->c) : 0; }
int SDL_CondBroadcast(SDL_cond *c) { return c ? pthread_cond_broadcast(&c->c) : 0; }
int SDL_CondWait(SDL_cond *c, SDL_mutex *m) { return (c && m) ? pthread_cond_wait(&c->c, &m->m) : -1; }

int SDL_AtomicGet(SDL_atomic_t *a) { return __atomic_load_n(&a->value, __ATOMIC_SEQ_CST); }
int SDL_AtomicSet(SDL_atomic_t *a, int v) { return __atomic_exchange_n(&a->value, v, __ATOMIC_SEQ_CST); }

/* ---- core --------------------------------------------------------------- */
int SDL_Init(Uint32 flags) { (void)flags; return 0; }
int SDL_InitSubSystem(Uint32 flags) { (void)flags; return 0; }
Uint32 SDL_WasInit(Uint32 flags) { (void)flags; return 0; }
void SDL_Quit(void) {}
void SDL_SetMainReady(void) {}
const char *SDL_GetError(void) { return "not available in the big-endian test build"; }
const char *SDL_GetHint(const char *name) { (void)name; return NULL; }
SDL_bool SDL_SetHintWithPriority(const char *n, const char *v, SDL_HintPriority p) {
    (void)n; (void)v; (void)p; return SDL_TRUE;
}
void SDL_free(void *p) { free(p); }
char *SDL_GetBasePath(void) { return strdup("./"); }
char *SDL_GetPrefPath(const char *org, const char *app) { (void)org; (void)app; return strdup("./"); }
SDL_RWops *SDL_RWFromFile(const char *file, const char *mode) { (void)file; (void)mode; return NULL; }

/* ---- events / keyboard -------------------------------------------------- */
int SDL_PollEvent(SDL_Event *e) { (void)e; return 0; }
int SDL_PushEvent(SDL_Event *e) { (void)e; return 0; }
const Uint8 *SDL_GetKeyboardState(int *numkeys) {
    static Uint8 keys[SDL_NUM_SCANCODES];
    if (numkeys) *numkeys = SDL_NUM_SCANCODES;
    return keys;
}

/* ---- video (never used: the test has no window) ------------------------- */
void SDL_DestroyWindow(SDL_Window *w) { (void)w; }
void SDL_MinimizeWindow(SDL_Window *w) { (void)w; }
void SDL_RestoreWindow(SDL_Window *w) { (void)w; }
void SDL_GetWindowSizeInPixels(SDL_Window *w, int *x, int *y) { (void)w; if (x) *x = 0; if (y) *y = 0; }
SDL_bool SDL_GetWindowWMInfo(SDL_Window *w, SDL_SysWMinfo *i) { (void)w; (void)i; return SDL_FALSE; }
void SDL_GL_DeleteContext(SDL_GLContext c) { (void)c; }
void SDL_GL_GetDrawableSize(SDL_Window *w, int *x, int *y) { (void)w; if (x) *x = 0; if (y) *y = 0; }
int SDL_GL_GetSwapInterval(void) { return 0; }
int SDL_GL_SetSwapInterval(int i) { (void)i; return -1; }

/* ---- audio: no device --------------------------------------------------- */
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *d, int c, const SDL_AudioSpec *want,
                                      SDL_AudioSpec *have, int changes) {
    (void)d; (void)c; (void)want; (void)have; (void)changes; return 0;
}
void SDL_PauseAudioDevice(SDL_AudioDeviceID d, int p) { (void)d; (void)p; }
void SDL_CloseAudioDevice(SDL_AudioDeviceID d) { (void)d; }

/* ---- controllers: none -------------------------------------------------- */
int SDL_NumJoysticks(void) { return 0; }
SDL_bool SDL_IsGameController(int i) { (void)i; return SDL_FALSE; }
SDL_GameController *SDL_GameControllerOpen(int i) { (void)i; return NULL; }
void SDL_GameControllerClose(SDL_GameController *g) { (void)g; }
SDL_bool SDL_GameControllerGetAttached(SDL_GameController *g) { (void)g; return SDL_FALSE; }
SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *g) { (void)g; return NULL; }
SDL_bool SDL_GameControllerHasRumble(SDL_GameController *g) { (void)g; return SDL_FALSE; }
const char *SDL_GameControllerName(SDL_GameController *g) { (void)g; return "none"; }
int SDL_GameControllerRumble(SDL_GameController *g, Uint16 a, Uint16 b, Uint32 c) {
    (void)g; (void)a; (void)b; (void)c; return -1;
}
int SDL_GameControllerAddMappingsFromRW(SDL_RWops *rw, int freerw) { (void)rw; (void)freerw; return 0; }
SDL_Joystick *SDL_JoystickFromInstanceID(SDL_JoystickID id) { (void)id; return NULL; }
SDL_JoystickID SDL_JoystickGetDeviceInstanceID(int i) { (void)i; return -1; }
SDL_JoystickGUID SDL_JoystickGetGUID(SDL_Joystick *j) { SDL_JoystickGUID g; (void)j; memset(&g, 0, sizeof(g)); return g; }
void SDL_JoystickGetGUIDString(SDL_JoystickGUID g, char *out, int n) { (void)g; if (out && n > 0) out[0] = '\0'; }
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *j) { (void)j; return -1; }
int SDL_JoystickAttachVirtual(SDL_JoystickType t, int a, int b, int h) { (void)t; (void)a; (void)b; (void)h; return -1; }
int SDL_JoystickDetachVirtual(int i) { (void)i; return -1; }
int SDL_JoystickSetVirtualAxis(SDL_Joystick *j, int a, Sint16 v) { (void)j; (void)a; (void)v; return -1; }
int SDL_JoystickSetVirtualButton(SDL_Joystick *j, int b, Uint8 v) { (void)j; (void)b; (void)v; return -1; }
