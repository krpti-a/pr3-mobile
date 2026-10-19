// In-level controls (main branch only). The original has one control scheme: a thumbstick (left/right/up/down) and an item
// button. The shell adds its own controls (joystick or d-pad, jump / crouch / item buttons, game controllers) and drives the
// game through the same input flags the thumbstick uses: LocalPlayer +0x2e4 up (jump), +0x2e5 down (crouch / charge a super
// jump), +0x2e6 left, +0x2e7 right. GamePlayMenu::UpdateThumbstick runs once per frame after the game's tick; it is wrapped to
// merge the shell's input into those flags and to place (or hide) the original thumbstick and item button.
#include "arm_rt.h"
#include "pr3_shell.h"

extern int rt_wide_off, rt_safe_left, rt_safe_right;
int rt_in_level(void);
void f_a1660_orig(CPU *c);

static int g_mask;                 // from the shell: bit0 left, bit1 right, bit2 up, bit3 down
static int g_show_stick = 1, g_show_item = 1, g_x_only;
// Position of the original item button in game points (x in the widened screen's own coordinates, 0 at its left edge); negative = the original place.
static double g_item_x = -1, g_item_y = -1;
void pr3_hud_item_pos(double x, double y) { g_item_x = x; g_item_y = y; }
static void put_y(uint32_t o, double v) { if (!o) return; uint64_t u; memcpy(&u, &v, 8); ST32(o + 0x24, (uint32_t)u); ST32(o + 0x28, (uint32_t)(u >> 32)); }

void pr3_input_set(int mask) { g_mask = mask; }
void pr3_hud_config(int show_stick, int show_item, int stick_x_only) { g_show_stick = show_stick; g_show_item = show_item; g_x_only = stick_x_only; }
int pr3_in_level(void) { return rt_in_level(); }

static uint32_t mainmgr(void) { uint32_t slot = 0x1b7b8 + LD32(0x1b83c); return LD32(slot) ? LD32(LD32(slot)) : 0; }
static double get_x(uint32_t o) { uint64_t u = ((uint64_t)LD32(o + 0x20) << 32) | LD32(o + 0x1c); double v; memcpy(&v, &u, 8); return v; }
static void put_x(uint32_t o, double v) { if (!o) return; uint64_t u; memcpy(&u, &v, 8); ST32(o + 0x1c, (uint32_t)u); ST32(o + 0x20, (uint32_t)(u >> 32)); }

// Use the held item by pressing the original item button: its click is what the game acts on. With the shell's own button the
// original one is parked out of sight (x = 3000) and tapped there. (GamePlayMenu::HandleEvent offers no direct call: its virtual
// function +0x50 is the retry action.)
static double g_btn_x = 3000, g_btn_y = 280;
extern int rt_wide_off;
void pr3_touch(int phase, int touch_id, float x, float y);
void pr3_use_item(void) {
    if (!rt_in_level()) return;
    float xp = (float)(320.0 - g_btn_y), yp = (float)(g_btn_x + rt_wide_off);      // game (landscape) point -> the view's portrait point
    pr3_touch(0, 9100, xp, yp);
    pr3_touch(3, 9100, xp, yp);
}

void wrap_GamePlayMenu_UpdateThumbstick(CPU *c) {
    uint32_t menu = c->r[0];
    // The original thumbstick (base +0xdd4, knob +0xdd0) and item button (+0xdd8): at the screen edges, or out of sight and reach.
    double stick = g_show_stick ? 64.0 - rt_wide_off + rt_safe_left : -3000.0;
    put_x(LD32(menu + 0xdd4), stick);
    if (!g_show_stick || get_x(LD32(menu + 0xdd0)) < -2000.0) put_x(LD32(menu + 0xdd0), stick);
    if (!g_show_item) put_x(LD32(menu + 0xdd8), 3000.0);
    else if (g_item_x < 0) { put_x(LD32(menu + 0xdd8), 440.0 + rt_wide_off - rt_safe_right); put_y(LD32(menu + 0xdd8), 280.0); }
    else { put_x(LD32(menu + 0xdd8), g_item_x - rt_wide_off); put_y(LD32(menu + 0xdd8), g_item_y); }
    g_btn_x = get_x(LD32(menu + 0xdd8));
    { uint64_t u = ((uint64_t)LD32(LD32(menu + 0xdd8) + 0x28) << 32) | LD32(LD32(menu + 0xdd8) + 0x24); memcpy(&g_btn_y, &u, 8); }
    f_a1660_orig(c);
    uint32_t mgr = mainmgr(), player = mgr ? LD32(mgr + 0x10c) : 0;
    if (!player || LD8(player + 0x18)) return;
    int up = LD8(player + 0x2e4), down = LD8(player + 0x2e5), left = LD8(player + 0x2e6), right = LD8(player + 0x2e7);
    if (g_x_only) up = down = 0;                               // the stick only steers sideways; jump / crouch have their own buttons
    up |= (g_mask >> 2) & 1; down |= (g_mask >> 3) & 1; left |= g_mask & 1; right |= (g_mask >> 1) & 1;
    ST8(player + 0x2e4, up); ST8(player + 0x2e5, down); ST8(player + 0x2e6, left); ST8(player + 0x2e7, right);
}
