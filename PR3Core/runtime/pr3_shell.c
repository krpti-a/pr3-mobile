#include "arm_rt.h"
#include "pr3_shell.h"

extern char rt_resource_root[1024];
extern char rt_documents_root[1024];
extern float rt_gl_scale;
extern void (*rt_shell_start_update)(void);
extern void (*rt_shell_end_update)(void);
void rt_run_initializers(void);
void hle_objc_register_image_classes(void);
uint32_t hle_objc_new_object(const char *cls);

// addresses of the original functions (see the symbol table of the armv7 binary)
enum {
    A_RK_MAIN_Init = 0xa8fc, A_RK_MAIN_Tick = 0xa3bc, A_RK_MAIN_Draw = 0xa25c,
    A_SetRKFrameTime = 0xa008, A_SetFrameTime = 0x9ff4, A_SetFrameRate = 0x9fe0, A_SetUpdateTime = 0xa01c,
    A_SetTouchPos = 0x9c04, A_SetNewTouch = 0x9a20, A_ClearTouch = 0x9b74, A_ResetTouchPos = 0x9ce8,
};

static float g_update_ms = 16.667f;

// A tap can begin and end between two display frames (the simulator's taps do); the game polls its input state
// once per Tick, so a release is deferred until the touch has been seen by at least one Tick.
#define MAXT 16
static struct { uint32_t id; int seen; int pending_end; } touches[MAXT];
static int ntouch;
static int find_touch(uint32_t id) { for (int i = 0; i < ntouch; i++) if (touches[i].id == id) return i; return -1; }
static void remove_touch(int i) { touches[i] = touches[--ntouch]; }
static void release_touch(uint32_t id);
void pr3_save(void);
static int g_booted;

int pr3_boot(const char *binary_path, const char *resource_root, const char *documents_root, float gl_scale) {
    snprintf(rt_resource_root, sizeof rt_resource_root, "%s", resource_root);
    snprintf(rt_documents_root, sizeof rt_documents_root, "%s", documents_root);
    rt_gl_scale = gl_scale;
    if (rt_init(binary_path)) return -1;
    hle_objc_register_image_classes();
    rt_run_initializers();
    uint32_t view = hle_objc_new_object("RK_EAGLView");
    rt_call(A_RK_MAIN_Init, &view, 1, NULL);
    g_booted = 1;
    return 0;
}

static void call_f(uint32_t fn, float v) { uint32_t a = BITS_F32(v); rt_call(fn, &a, 1, NULL); }

void rt_objc_fire_timers(double dt_ms);
void pr3_frame(double dt_ms) {
    if (!g_booted) return;
    // The original measured the time between successive frames; clamp like a stalled display link would.
    if (dt_ms > 100.0) dt_ms = 100.0;
    if (dt_ms > 0) g_update_ms = (float)dt_ms;
    rt_objc_fire_timers(g_update_ms);          // NSTimers run on the main run loop between frames
    rt_call(A_RK_MAIN_Tick, NULL, 0, NULL);
    for (int i = 0; i < ntouch; ) {
        touches[i].seen = 1;
        if (touches[i].pending_end) { uint32_t id = touches[i].id; remove_touch(i); release_touch(id); } else i++;
    }
    if (rt_shell_start_update) rt_shell_start_update();
    { extern void rt_gl_frame_begin(void); rt_gl_frame_begin(); }
    rt_call(A_RK_MAIN_Draw, NULL, 0, NULL);
    call_f(A_SetRKFrameTime, g_update_ms);
    if (rt_shell_end_update) rt_shell_end_update();
    call_f(A_SetFrameTime, g_update_ms);
    call_f(A_SetFrameRate, 1000.0f / g_update_ms);
    call_f(A_SetUpdateTime, g_update_ms);
    { void rt_objc_drain_pool(void); rt_objc_drain_pool(); }       // the original drained its autorelease pool every run-loop iteration
}

static void release_touch(uint32_t id) {
    rt_call(A_ClearTouch, &id, 1, NULL);
    rt_call(A_ResetTouchPos, &id, 1, NULL);
}

void pr3_touch(int phase, int touch_id, float x, float y) {
    if (!g_booted) return;
    uint32_t id = (uint32_t)touch_id;
    if (phase == 0 && ntouch < MAXT) { touches[ntouch].id = id; touches[ntouch].seen = 0; touches[ntouch].pending_end = 0; ntouch++; }
    if (phase == 0) {
        uint32_t a[3] = { (uint32_t)(int32_t)x, (uint32_t)(int32_t)y, id };
        rt_call(A_SetTouchPos, a, 3, NULL);
        rt_call(A_SetNewTouch, &id, 1, NULL);
    } else if (phase == 1) {
        uint32_t a[3] = { (uint32_t)(int32_t)x, (uint32_t)(int32_t)y, id };
        rt_call(A_SetTouchPos, a, 3, NULL);
    } else if (phase == 3 || phase == 4) {
        int i = find_touch(id);
        if (i >= 0 && !touches[i].seen) { touches[i].pending_end = 1; return; }
        if (i >= 0) remove_touch(i);
        release_touch(id);
    }
}

// The original saved its Settings.dat (character, unlocked parts, campaign progress) in -applicationWillResignActive:
// (0x8408). iOS apps are suspended or killed without further notice, so the shell calls this when the app leaves the
// foreground and every few seconds while playing (a wall-clock timer in the shell).
void pr3_save(void) {
    if (!g_booted) return;
    uint32_t slot = 0x843c + LD32(0x846c), settings = LD32(LD32(slot));
    if (settings) rt_call(0x63740, &settings, 1, NULL);         // Settings::SaveSettings
}

void pr3_set_view_hooks(pr3_hook s, pr3_hook e) { rt_shell_start_update = s; rt_shell_end_update = e; }
float pr3_gl_scale(void) { return rt_gl_scale; }
