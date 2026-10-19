// Widescreen gameplay (main branch only). hle_gl.c widens the viewport and projection while a level is shown (rt_wide_off
// points on each side of the 480-point game screen); the wrappers here widen what the game itself sizes to 480 x 320.
#include "arm_rt.h"

extern int rt_wide_off;
void f_1933c_orig(CPU *c);
void f_a0de0_orig(CPU *c);
void f_a1310_orig(CPU *c);
void f_8e250_orig(CPU *c);

// MainMgr is the level background sprite. It is a 480 x 320 picture: scale it up (keeping its centre) until it covers the
// widened screen too.
void wrap_MainMgr_SetBG(CPU *c) {
    uint32_t mm = c->r[0];
    f_1933c_orig(c);
    if (rt_wide_off > 0) {                                   // sprite position = centre (240, 160); scale at +0x28 / +0x2c
        float f = (480.0f + 2 * rt_wide_off) / 480.0f;
        ST32(mm + 0x28, BITS_F32(LDF(mm + 0x28) * f)); ST32(mm + 0x2c, BITS_F32(LDF(mm + 0x2c) * f));
    }
}

// ---- In-level controls. The original places them with positions (centre x, y as doubles at +0x1c / +0x24) given by literals of
// its code. On a wider screen the left group (back, retry, thumbstick) moves to the left edge of the screen, the right group (item
// button, timer) to the right edge. The positions are shifted after the objects are made; the thumbstick works from its
// object's position, so its touch area follows.
static void shift_x(uint32_t obj, int dx) {
    if (!obj) return;
    uint64_t bits = (uint64_t)LD32(obj + 0x20) << 32 | LD32(obj + 0x1c); double v; memcpy(&v, &bits, 8);
    v += dx; memcpy(&bits, &v, 8);
    ST32(obj + 0x1c, (uint32_t)bits); ST32(obj + 0x20, (uint32_t)(bits >> 32));
}

// GamePlayMenu::GamePlayMenu(): back button +0xda8, thumbstick base +0xdd4 and knob +0xdd0, item button +0xdd8
void wrap_GamePlayMenu_ctor(CPU *c) {
    uint32_t menu = c->r[0];
    f_a0de0_orig(c);
    if (rt_wide_off <= 0) return;
    shift_x(LD32(menu + 0xda8), -rt_wide_off); shift_x(LD32(menu + 0xdd4), -rt_wide_off); shift_x(LD32(menu + 0xdd0), -rt_wide_off);
    shift_x(LD32(menu + 0xdd8), +rt_wide_off);
}

// GamePlayMenu::AddRetryButton(): retry button +0xddc (made on the first call)
void wrap_GamePlayMenu_AddRetryButton(CPU *c) {
    uint32_t menu = c->r[0], before = LD32(menu + 0xddc);
    f_a1310_orig(c);
    if (rt_wide_off > 0 && !before) shift_x(LD32(menu + 0xddc), -rt_wide_off);
}

// GameTimer::Draw sets its text field's position every frame, then calls ActionContainer::Draw on itself: shift it there.
void wrap_ActionContainer_Draw(CPU *c) {
    enum { VT_GameTimer = 0x127a3c };
    uint32_t o = c->r[0];
    if (rt_wide_off > 0 && o && LD32(o) == VT_GameTimer) shift_x(LD32(o + 0x6d8), +rt_wide_off);
    f_8e250_orig(c);
}

// BlockMap::Render draws 7 columns (40 points each) either side of the camera, enough for the 480-point screen. When the camera is
// turned by 90 degrees (rows 7 / 7) the screen's long axis runs along the rows instead.
int rt_wide_cols(void) { return rt_wide_off > 0 ? (rt_wide_off + 39) / 40 : 0; }
int rt_wide_rows(uint32_t rows) { return rows == 7 ? rt_wide_cols() : 0; }
