// C API used by the Swift app shell to drive the recompiled original game.
#ifndef PR3_SHELL_H
#define PR3_SHELL_H
#include <stdint.h>

typedef void (*pr3_hook)(void);

// Loads the original binary image, applies dyld binds, runs C++ static initialisers and RK_MAIN_Init.
int  pr3_boot(const char *binary_path, const char *resource_root, const char *documents_root, float gl_scale);
// One display frame (the original RK_EAGLView updateView): Tick -> startUpdateView -> Draw -> endUpdateView.
void pr3_frame(double dt_ms);
// Touch phases follow UITouchPhase: 0 began, 1 moved, 3 ended, 4 cancelled. (x, y) are in the view's local
// portrait coordinate space (320 x 480), exactly what -[UITouch locationInView:] returned in the original.
void pr3_touch(int phase, int touch_id, float x, float y);
void pr3_set_view_hooks(pr3_hook start_update, pr3_hook end_update);
// Writes Settings.dat (what the original did in applicationWillResignActive). Call when the app leaves the foreground.
void pr3_save(void);
float pr3_gl_scale(void);
#endif
