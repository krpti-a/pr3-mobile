// OpenGL ES 1.1 calls made by the original engine, forwarded to the host GL (or logged when RT_GL_NULL).
#include "arm_rt.h"
#include <time.h>
#if defined(RT_GL_REAL)
  #if defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE
      #include <OpenGLES/ES1/gl.h>
      #include <OpenGLES/ES1/glext.h>
      #define GL_OES 1
    #else
      #define GL_SILENCE_DEPRECATION 1
      #include <OpenGL/gl.h>
      #include <OpenGL/glext.h>
    #endif
  #endif
#endif

#ifndef RT_GL_REAL
typedef unsigned int GLenum, GLuint; typedef int GLint, GLsizei; typedef float GLfloat;
#define glClearColor(...) 0
#define glClear(...) 0
#define glEnable(...) 0
#define glDisable(...) 0
#define glBlendFunc(...) 0
#define glEnableClientState(...) 0
#define glDisableClientState(...) 0
#define glHint(...) 0
#define glTexParameteri(...) 0
#define glTexParameterf(...) 0
#define glLineWidth(...) 0
#define glColor4f(...) 0
#define glMatrixMode(...) 0
#define glLoadIdentity(...) 0
#define glPushMatrix(...) 0
#define glPopMatrix(...) 0
#define glTranslatef(...) 0
#define glScalef(...) 0
#define glRotatef(...) 0
#define glOrthof(...) 0
#define glViewport(...) 0
#define glBindTexture(...) 0
#define glFlush(...) 0
#define glVertexPointer(...) 0
#define glColorPointer(...) 0
#define glTexCoordPointer(...) 0
#define glDrawArrays(...) 0
#define glTexImage2D(...) 0
#define glReadPixels(...) 0
#define glBindFramebufferOES(...) 0
#define glBindRenderbufferOES(...) 0
#define glRenderbufferStorageOES(...) 0
#define glFramebufferRenderbufferOES(...) 0
#endif
static float AF(CPU *c, int i) { return F32_BITS(c->r[i]); }
static float SF(CPU *c, int i) { return LDF(c->sp + 4 * i); }      // float argument on the stack
uint32_t rt_gl_calls, rt_gl_draws, rt_gl_reads; double rt_gl_read_ms;
float rt_gl_scale = 1.0f;
int rt_wide_off;                 // widescreen: points added on each side of the 480-point game screen (see "Widescreen" below)
static int vp_scaled = 1;        // the 320x480 screen viewport is scaled; the 480x320 art-capture viewport (block pictures) is not      // framebuffer pixels per game point (the game addresses a 320 x 480 portrait screen)
static int gl_log_budget = 0;
void rt_gl_set_log(int n) { gl_log_budget = n; }
#define GLOG(...) do { if (gl_log_budget > 0) { gl_log_budget--; fprintf(stderr, "[gl] " __VA_ARGS__); fputc('\n', stderr); } } while (0)

#ifdef RT_GL_REAL
#define OES(name) name##OES
#if !defined(GL_OES)
  #define glGenFramebuffersOES glGenFramebuffersEXT
  #define glBindFramebufferOES glBindFramebufferEXT
  #define glDeleteFramebuffersOES glDeleteFramebuffersEXT
  #define glGenRenderbuffersOES glGenRenderbuffersEXT
  #define glBindRenderbufferOES glBindRenderbufferEXT
  #define glDeleteRenderbuffersOES glDeleteRenderbuffersEXT
  #define glRenderbufferStorageOES glRenderbufferStorageEXT
  #define glFramebufferRenderbufferOES glFramebufferRenderbufferEXT
  #define glCheckFramebufferStatusOES glCheckFramebufferStatusEXT
  #define glGetRenderbufferParameterivOES glGetRenderbufferParameterivEXT
  #define glOrthof(l, r, b, t, n, f) glOrtho(l, r, b, t, n, f)
#endif
#define G(stmt) do { rt_gl_calls++; stmt; } while (0)
#else
#define G(stmt) do { rt_gl_calls++; } while (0)
#endif

void hle__glClearColor(CPU *c) { GLOG("glClearColor %g %g %g %g", AF(c,0), AF(c,1), AF(c,2), AF(c,3)); G(glClearColor(AF(c,0), AF(c,1), AF(c,2), AF(c,3))); }
// Start of a frame: the strips beside the 480-point menu area (widescreen) are never drawn by the game.
void rt_gl_clear_frame(void) { if (rt_wide_off > 0) { G(glClearColor(0, 0, 0, 1)); G(glClear(0x4000)); } }
void hle__glClear(CPU *c) { GLOG("glClear 0x%x", c->r[0]); G(glClear(c->r[0])); }
void hle__glEnable(CPU *c) { GLOG("glEnable 0x%x", c->r[0]); G(glEnable(c->r[0])); }
void hle__glDisable(CPU *c) { GLOG("glDisable 0x%x", c->r[0]); G(glDisable(c->r[0])); }
void hle__glBlendFunc(CPU *c) { GLOG("glBlendFunc 0x%x 0x%x", c->r[0], c->r[1]); G(glBlendFunc(c->r[0], c->r[1])); }
void hle__glEnableClientState(CPU *c) { G(glEnableClientState(c->r[0])); }
void hle__glDisableClientState(CPU *c) { G(glDisableClientState(c->r[0])); }
void hle__glHint(CPU *c) { G(glHint(c->r[0], c->r[1])); }
void hle__glTexParameteri(CPU *c) { G(glTexParameteri(c->r[0], c->r[1], (GLint)c->r[2])); }
void hle__glTexParameterf(CPU *c) { G(glTexParameterf(c->r[0], c->r[1], AF(c, 2))); }
void hle__glLineWidth(CPU *c) { G(glLineWidth(AF(c, 0) * (vp_scaled ? rt_gl_scale : 1.0f))); }
void hle__glColor4f(CPU *c) { G(glColor4f(AF(c,0), AF(c,1), AF(c,2), AF(c,3))); }
static uint32_t gl_matrix_mode = 0x1700;
void hle__glMatrixMode(CPU *c) { gl_matrix_mode = c->r[0]; G(glMatrixMode(c->r[0])); }
void hle__glLoadIdentity(CPU *c) { (void)c; G(glLoadIdentity()); }
void hle__glPushMatrix(CPU *c) { (void)c; GLOG("glPushMatrix"); G(glPushMatrix()); }
void hle__glPopMatrix(CPU *c) { (void)c; GLOG("glPopMatrix"); G(glPopMatrix()); }
void hle__glTranslatef(CPU *c) { GLOG("glTranslatef %g %g %g", AF(c,0), AF(c,1), AF(c,2)); G(glTranslatef(AF(c,0), AF(c,1), AF(c,2))); }
void hle__glScalef(CPU *c) { GLOG("glScalef %g %g %g", AF(c,0), AF(c,1), AF(c,2)); G(glScalef(AF(c,0), AF(c,1), AF(c,2))); }
void hle__glRotatef(CPU *c) { GLOG("glRotatef %g %g %g %g", AF(c,0), AF(c,1), AF(c,2), AF(c,3)); G(glRotatef(AF(c,0), AF(c,1), AF(c,2), AF(c,3))); }
// ---- Widescreen (main branch). The game's screen is 480 x 320 landscape points. On a wider display the framebuffer is
// 480 + 2*rt_wide_off points wide. In menus the game keeps its 480-point area, centred; while a level is shown the viewport
// and the projection grow by rt_wide_off points on both sides, so the world is simply drawn wider (the game's coordinates,
// camera and touch handling stay those of the 480-point screen; the shell shifts touches by rt_wide_off).
int rt_in_level(void);
static int wide_game;                 // the last screen viewport was the widened one
static int ortho_widened;             // the projection was widened and the original must be restored for off-screen drawing
static float ortho_orig[6];
void hle__glOrthof(CPU *c) {
    float a[6] = { AF(c,0), AF(c,1), AF(c,2), AF(c,3), SF(c,0), SF(c,1) };
    GLOG("glOrthof %g %g %g %g %g %g", a[0], a[1], a[2], a[3], a[4], a[5]);
    memcpy(ortho_orig, a, sizeof a); ortho_widened = 0;
    if (wide_game && a[0] == 0 && a[3] == 0) { float o = rt_wide_off * (a[2] / 480.0f); a[2] += o; a[3] -= o; ortho_widened = 1; }
    G(glOrthof(a[0], a[1], a[2], a[3], a[4], a[5]));
}
static uint32_t game_vp[4] = {0, 0, 320, 480};          // the viewport as the game set it (in its own units)
void hle__glViewport(CPU *c) {
    for (int i = 0; i < 4; i++) game_vp[i] = c->r[i];
    GLOG("glViewport %d %d %d %d", (int)c->r[0], (int)c->r[1], (int)c->r[2], (int)c->r[3]);
    vp_scaled = (c->r[2] == 320 && c->r[3] == 480);      // the game's real screen: 320 x 480 points
    float k = vp_scaled ? rt_gl_scale : 1.0f;
    wide_game = 0;
    if (vp_scaled && rt_wide_off > 0) {
        if (rt_in_level()) { wide_game = 1; G(glViewport(0, 0, (GLsizei)(320 * k), (GLsizei)((480 + 2 * rt_wide_off) * k))); return; }
        G(glViewport(0, (GLint)(rt_wide_off * k), (GLsizei)(320 * k), (GLsizei)(480 * k))); return;
    }
    if (!vp_scaled && ortho_widened) {                   // off-screen drawing (minimap, block art) sets its own viewport after Mode2D: undo the widening
        G(glMatrixMode(0x1701)); G(glLoadIdentity()); G(glOrthof(ortho_orig[0], ortho_orig[1], ortho_orig[2], ortho_orig[3], ortho_orig[4], ortho_orig[5]));
        G(glMatrixMode(gl_matrix_mode)); ortho_widened = 0;
    }
    G(glViewport((GLint)(c->r[0] * k), (GLint)(c->r[1] * k), (GLsizei)(c->r[2] * k), (GLsizei)(c->r[3] * k)));
}
static uint32_t cur_tex;
void hle__glBindTexture(CPU *c) { cur_tex = c->r[1]; GLOG("bind tex %u", c->r[1]); G(glBindTexture(c->r[0], c->r[1])); }
void hle__glFlush(CPU *c) { (void)c; G(glFlush()); }
static uint32_t last_vptr, last_vsize, last_vstride;
void hle__glVertexPointer(CPU *c) { last_vptr = c->r[3]; last_vsize = c->r[0]; last_vstride = c->r[2]; G(glVertexPointer((GLint)c->r[0], c->r[1], (GLsizei)c->r[2], rt_ptr(c->r[3]))); }
void hle__glColorPointer(CPU *c) { G(glColorPointer((GLint)c->r[0], c->r[1], (GLsizei)c->r[2], rt_ptr(c->r[3]))); }
void hle__glTexCoordPointer(CPU *c) { G(glTexCoordPointer((GLint)c->r[0], c->r[1], (GLsizei)c->r[2], rt_ptr(c->r[3]))); }
// Debugging aid (gl_boot PR3_DRAW_LIMIT/PR3_LIMIT_FRAME): renders only the first N draw calls of every frame (bisects which draw produces an artifact).
static uint32_t frame_draws; static int draw_limit = -1;
void rt_gl_frame_begin(void) { frame_draws = 0; }
void rt_gl_set_draw_limit(int n) { draw_limit = n; }
void hle__glDrawArrays(CPU *c) { rt_gl_draws++;
    if (draw_limit >= 0 && frame_draws++ >= (uint32_t)draw_limit) return; if (gl_log_budget > 0 && last_vptr) { uint32_t st = last_vstride ? last_vstride : last_vsize * 4; GLOG("draw mode %u first %d count %d  v0=(%g,%g) v1=(%g,%g)", c->r[0], (int)c->r[1], (int)c->r[2], LDF(last_vptr + c->r[1] * st), LDF(last_vptr + c->r[1] * st + 4), c->r[2] > 1 ? LDF(last_vptr + (c->r[1] + 1) * st) : 0.f, c->r[2] > 1 ? LDF(last_vptr + (c->r[1] + 1) * st + 4) : 0.f); } G(glDrawArrays(c->r[0], (GLint)c->r[1], (GLsizei)c->r[2])); }
void hle__glGenTextures(CPU *c) {
#ifdef RT_GL_REAL
    GLuint ids[64]; GLsizei n = (GLsizei)c->r[0]; if (n > 64) n = 64; glGenTextures(n, ids); for (int i = 0; i < n; i++) ST32(c->r[1] + 4 * i, ids[i]);
#else
    static uint32_t next = 1; for (uint32_t i = 0; i < c->r[0]; i++) ST32(c->r[1] + 4 * i, next++);
#endif
    rt_gl_calls++;
}
void hle__glDeleteTextures(CPU *c) {
#ifdef RT_GL_REAL
    GLuint ids[64]; GLsizei n = (GLsizei)c->r[0]; if (n > 64) n = 64; for (int i = 0; i < n; i++) ids[i] = LD32(c->r[1] + 4 * i); glDeleteTextures(n, ids);
#endif
    rt_gl_calls++;
}
void hle__glTexImage2D(CPU *c) {
    // (target, level, internalformat, width | height, border, format, type, pixels): width in r3, the rest on the stack
    uint32_t w = c->r[3], h = LD32(c->sp), border = LD32(c->sp + 4), fmt = LD32(c->sp + 8), type = LD32(c->sp + 12), pix = LD32(c->sp + 16);
    (void)border;
    GLOG("glTexImage2D %ux%u ifmt 0x%x fmt 0x%x type 0x%x", w, h, c->r[2], fmt, type);
    { extern char rt_last_fopen[]; if (getenv("PR3_TEXLOG")) fprintf(stderr, "[tex] id=%u %ux%u <- %s lr=%x\n", cur_tex, w, h, rt_last_fopen, c->lr); }
    G(glTexImage2D(c->r[0], (GLint)c->r[1], (GLint)c->r[2], (GLsizei)w, (GLsizei)h, 0, fmt, type, pix ? rt_ptr(pix) : NULL));
}
void hle__glReadPixels(CPU *c) {
    // (x, y, w, h, format, type, pixels) -> format/type/pixels on the stack
    uint32_t fmt = LD32(c->sp), type = LD32(c->sp + 4), pix = LD32(c->sp + 8);
    (void)fmt; (void)type; (void)pix;
    rt_gl_reads++;
    GLOG("glReadPixels %d %d %d %d", (int)c->r[0], (int)c->r[1], (int)c->r[2], (int)c->r[3]);
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    G(glReadPixels((GLint)c->r[0], (GLint)c->r[1], (GLsizei)c->r[2], (GLsizei)c->r[3], fmt, type, rt_ptr(pix)));
    clock_gettime(CLOCK_MONOTONIC, &t1); rt_gl_read_ms += (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    if (getenv("PR3_DUMP_READ")) {       // debugging aid: dump every framebuffer read-back
        static int n; char name[128]; snprintf(name, sizeof name, "%s/read_%03d_%dx%d.pam", getenv("PR3_DUMP_READ"), n++, (int)c->r[2], (int)c->r[3]);
        FILE *f = fopen(name, "wb"); if (f) { fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", (int)c->r[2], (int)c->r[3]); fwrite(rt_ptr(pix), 4, c->r[2] * c->r[3], f); fclose(f); }
    }
}
void hle__glGetIntegerv(CPU *c) {
    if (c->r[0] == 0xba2) {              // GL_VIEWPORT: four integers; RK_SPRITE::GetFrameBufferImage derives its read-back row from the height
        for (int i = 0; i < 4; i++) ST32(c->r[1] + 4 * i, game_vp[i]);
    } else {
#ifdef RT_GL_REAL
        GLint v[4] = {0}; glGetIntegerv(c->r[0], v); ST32(c->r[1], (uint32_t)v[0]);
#else
        ST32(c->r[1], 0);
#endif
    }
    rt_gl_calls++;
}
void hle__glGenFramebuffersOES(CPU *c) { uint32_t v = 0; (void)v;
#ifdef RT_GL_REAL
    GLuint id; glGenFramebuffersOES(1, &id); ST32(c->r[1], id);
#endif
    rt_gl_calls++; }
void hle__glBindFramebufferOES(CPU *c) { G(glBindFramebufferOES(c->r[0], c->r[1])); }
void hle__glGenRenderbuffersOES(CPU *c) {
#ifdef RT_GL_REAL
    GLuint id; glGenRenderbuffersOES(1, &id); ST32(c->r[1], id);
#endif
    rt_gl_calls++; }
void hle__glBindRenderbufferOES(CPU *c) { G(glBindRenderbufferOES(c->r[0], c->r[1])); }
void hle__glRenderbufferStorageOES(CPU *c) { G(glRenderbufferStorageOES(c->r[0], c->r[1], (GLsizei)c->r[2], (GLsizei)c->r[3])); }
void hle__glFramebufferRenderbufferOES(CPU *c) { G(glFramebufferRenderbufferOES(c->r[0], c->r[1], c->r[2], c->r[3])); }
void hle__glCheckFramebufferStatusOES(CPU *c) {
#ifdef RT_GL_REAL
    c->r[0] = glCheckFramebufferStatusOES(c->r[0]);
#else
    c->r[0] = 0x8cd5;
#endif
}
void hle__glGetRenderbufferParameterivOES(CPU *c) {
#ifdef RT_GL_REAL
    GLint v = 0; glGetRenderbufferParameterivOES(c->r[0], c->r[1], &v); ST32(c->r[2], (uint32_t)v);
#else
    ST32(c->r[2], 0);
#endif
}
void hle__glDeleteFramebuffersOES(CPU *c) { (void)c; }
void hle__glDeleteRenderbuffersOES(CPU *c) { (void)c; }
