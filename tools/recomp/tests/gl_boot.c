// Headless render harness: runs the recompiled game through the same pr3_boot/pr3_frame/pr3_touch API the app uses,
// with a real (macOS legacy) GL context, and dumps frames as PPM.
//   gl_boot <PR3.armv7> <frames> [every]
//   env: PR3_RES (resource dir, required)  PR3_DOCS (documents dir)  PR3_OUT (frame dir)  SCALE (default 2)
//        TAPS="frame:x:y,..."        taps in 480x320 landscape game coordinates
//        HOLDS="f1:f2:x:y,..."       finger held from frame f1 to f2
//        FINISH_AT=frame            call OfflineGamePage::IFinished (the player reaches the finish)
//        SAVE_FRAMES="f1,f2,..."   save only these frames
//        GLOG / GLOG_AT / GLOG_N     GL call log;  PR3_DRAW_LIMIT / PR3_LIMIT_FRAME  render only the first N draws per frame
#define GL_SILENCE_DEPRECATION 1
#include "arm_rt.h"
#include "pr3_shell.h"
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <sys/resource.h>

void rt_gl_set_log(int);
void rt_gl_set_draw_limit(int);
extern uint32_t rt_gl_calls, rt_gl_draws;

static void save_ppm(const char *path, int w, int h) {
    uint8_t *px = malloc(w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) for (int x = 0; x < w; x++) fwrite(px + (y * w + x) * 4, 1, 3, f);
    fclose(f); free(px);
}

// landscape game point -> portrait view-local point (the original rotated its 320x480 view by 90 degrees)
static int wide_off;
static void to_portrait(int xl, int yl, float *xp, float *yp) { *xp = (float)(320 - yl); *yp = (float)(xl + wide_off); }

int main(int argc, char **argv) {
    float SC = getenv("SCALE") ? atof(getenv("SCALE")) : 2.0f;
    wide_off = getenv("WIDE") ? atoi(getenv("WIDE")) : 0;
    int W = (int)(320 * SC), H = (int)((480 + 2 * wide_off) * SC);
    CGLPixelFormatAttribute attrs[] = { kCGLPFAAccelerated, kCGLPFAColorSize, (CGLPixelFormatAttribute)24, (CGLPixelFormatAttribute)0 };
    CGLPixelFormatObj pf; GLint n; CGLChoosePixelFormat(attrs, &pf, &n);
    CGLContextObj ctx; CGLCreateContext(pf, NULL, &ctx); CGLSetCurrentContext(ctx);
    GLuint fbo, rb; glGenFramebuffersEXT(1, &fbo); glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
    glGenRenderbuffersEXT(1, &rb); glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, W, H);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
    fprintf(stderr, "fbo status 0x%x\n", glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT));

    const char *docs = getenv("PR3_DOCS") ? getenv("PR3_DOCS") : "/tmp/pr3_docs";
    if (pr3_boot(argv[1], getenv("PR3_RES"), docs, SC)) { fprintf(stderr, "boot failed\n"); return 1; }
    pr3_set_wide(wide_off);
    if (getenv("GLOG")) rt_gl_set_log(atoi(getenv("GLOG")));
    { extern uint32_t rt_watch_lo, rt_watch_len; if (getenv("RT_WATCH")) { rt_watch_lo = (uint32_t)strtoul(getenv("RT_WATCH"), 0, 16); rt_watch_len = 16; } }
    int frames = argc > 2 ? atoi(argv[2]) : 3, every = argc > 3 ? atoi(argv[3]) : 1;

    int tap_f[64], tap_x[64], tap_y[64], ntaps = 0;
    if (getenv("TAPS")) { char *s = strdup(getenv("TAPS")); for (char *tok = strtok(s, ","); tok && ntaps < 64; tok = strtok(NULL, ",")) { sscanf(tok, "%d:%d:%d", &tap_f[ntaps], &tap_x[ntaps], &tap_y[ntaps]); ntaps++; } }
    int hold_f1[16], hold_f2[16], hold_x[16], hold_y[16], nholds = 0;
    if (getenv("HOLDS")) { char *s2 = strdup(getenv("HOLDS")); for (char *tok = strtok(s2, ","); tok && nholds < 16; tok = strtok(NULL, ",")) { sscanf(tok, "%d:%d:%d:%d", &hold_f1[nholds], &hold_f2[nholds], &hold_x[nholds], &hold_y[nholds]); nholds++; } }
    int drag_f1[8], drag_f2[8], drag_x1[8], drag_y1[8], drag_x2[8], drag_y2[8], ndrags = 0;     // DRAGS="f1:f2:x1:y1:x2:y2,..." swipe in landscape game coordinates
    if (getenv("DRAGS")) { char *s3 = strdup(getenv("DRAGS")); for (char *tok = strtok(s3, ","); tok && ndrags < 8; tok = strtok(NULL, ",")) { sscanf(tok, "%d:%d:%d:%d:%d:%d", &drag_f1[ndrags], &drag_f2[ndrags], &drag_x1[ndrags], &drag_y1[ndrags], &drag_x2[ndrags], &drag_y2[ndrags]); ndrags++; } }
    int dl_frame = getenv("PR3_LIMIT_FRAME") ? atoi(getenv("PR3_LIMIT_FRAME")) : 1 << 30, dl = getenv("PR3_DRAW_LIMIT") ? atoi(getenv("PR3_DRAW_LIMIT")) : -1;
    const char *out = getenv("PR3_OUT") ? getenv("PR3_OUT") : ".";

    extern double rt_virtual_now;
    for (int i = 0; i < frames; i++) {
        rt_virtual_now = 1.0e6 + i / 30.0;
        if (i == dl_frame) rt_gl_set_draw_limit(dl);
        for (int k = 0; k < ntaps; k++) {
            float xp, yp; to_portrait(tap_x[k], tap_y[k], &xp, &yp);
            if (i == tap_f[k]) pr3_touch(0, 100 + k, xp, yp);
            if (i == tap_f[k] + 4) pr3_touch(3, 100 + k, xp, yp);
        }
        for (int k = 0; k < nholds; k++) {
            float xp, yp; to_portrait(hold_x[k], hold_y[k], &xp, &yp);
            if (i == hold_f1[k]) pr3_touch(0, 500 + k, xp, yp);
            else if (i > hold_f1[k] && i < hold_f2[k]) pr3_touch(1, 500 + k, xp, yp);
            else if (i == hold_f2[k]) pr3_touch(3, 500 + k, xp, yp);
        }
        for (int k = 0; k < ndrags; k++) {
            if (i < drag_f1[k] || i > drag_f2[k]) continue;
            float t = drag_f2[k] > drag_f1[k] ? (float)(i - drag_f1[k]) / (drag_f2[k] - drag_f1[k]) : 1.0f, xp, yp;
            to_portrait((int)(drag_x1[k] + (drag_x2[k] - drag_x1[k]) * t), (int)(drag_y1[k] + (drag_y2[k] - drag_y1[k]) * t), &xp, &yp);
            pr3_touch(i == drag_f1[k] ? 0 : (i == drag_f2[k] ? 3 : 1), 700 + k, xp, yp);
        }
        if (getenv("FINISH_AT") && i == atoi(getenv("FINISH_AT"))) {      // test aid: make the player cross the finish line (OfflineGamePage::IFinished)
            uint32_t slot = 0x1b7b8 + LD32(0x1b83c), mainmgr = LD32(LD32(slot)), page = LD32(mainmgr + 0x104);
            fprintf(stderr, "[test] finishing level: MainMgr 0x%x page 0x%x\n", mainmgr, page);
            if (page) rt_call(0xdef48, &page, 1, NULL);
        }
        if (getenv("GLOG_AT") && i == atoi(getenv("GLOG_AT"))) rt_gl_set_log(getenv("GLOG_N") ? atoi(getenv("GLOG_N")) : 400);
        pr3_frame(1000.0 / 30.0);
        int want = i % every == 0;
        if (getenv("SAVE_FRAMES")) { want = 0; char *sf = strdup(getenv("SAVE_FRAMES")); for (char *tok = strtok(sf, ","); tok; tok = strtok(NULL, ",")) if (atoi(tok) == i) want = 1; free(sf); }
        if (want) { char p[512]; snprintf(p, sizeof p, "%s/frame_%03d.ppm", out, i); save_ppm(p, W, H); }
    }
    if (getenv("PR3_DEBUG_MODES")) { void pr3_modes_dump(void); pr3_modes_dump(); }
    pr3_save();
    { void rt_objc_stats(void); rt_objc_stats(); }
    { struct rusage ru; getrusage(RUSAGE_SELF, &ru); fprintf(stderr, "gl calls %u draws %u maxrss %.1f MB\n", rt_gl_calls, rt_gl_draws, ru.ru_maxrss / 1048576.0); }
    return 0;
}
