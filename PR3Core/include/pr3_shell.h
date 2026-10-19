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
// Widescreen: the view is 320 x (480 + 2 * off) points; the game's own 480-point screen sits in the middle of it, and while a
// level is shown the world is drawn over the whole view. Call before the first frame; touches stay in view coordinates.
void pr3_set_wide(int off);
// The display's unsafe borders (notch, rounded corners) in game points: the in-level controls stay inside them.
void pr3_set_safe(int left, int right);

// ---- In-level controls (see pr3_controls.c) ----
// The shell's own controls drive the game's input flags: mask bit0 left, bit1 right, bit2 up (jump), bit3 down (crouch / super jump).
void pr3_input_set(int mask);
void pr3_use_item(void);
// Which of the original's controls stay on screen: the thumbstick, the item button; and whether the thumbstick steers sideways only.
void pr3_hud_config(int show_stick, int show_item, int stick_x_only);
void pr3_hud_item_pos(double x, double y);   // item button centre in the widened screen's game points (x < 0: original place)
int  pr3_in_level(void);
#endif
