// Fixes for defects of the original app that the faithful port would otherwise reproduce.
// Wrapped original functions are listed under "wraps" in tools/recomp/full.json (see gen.py).
#include "arm_rt.h"

void f_363dc_orig(CPU *c);
void f_e89a0_orig(CPU *c);

// Player::ShowHats() re-applies the hat colours only when it also reloads a hat sprite (dirty flags at this+0x16b..0x171),
// so a changed hat colour showed up only after switching to another hat. Mark every slot dirty so the sprites (and their
// colours) are rebuilt whenever the appearance is refreshed.
void wrap_Player_ShowHats(CPU *c) {
    uint32_t player = c->r[0];
    for (int i = 0; i < 7; i++) ST8(player + 0x16b + i, 1);
    f_363dc_orig(c);
}

// Minimap::CreateMinimap fits the level into the 60 x 118 minimap area (scale at this+0x6f0, used for the player dots) but clamps the
// scale used to draw the blocks and the finish dots (this+0x708) to at least 0.025, a clamp the dots' scale never gets. On every
// level big enough to need a smaller scale (about a third of the single-player levels) the blocks are drawn too big, run past the
// 64 x 128 read-back area (the finish dots, drawn there too, vanish) and no longer line up with the player dots. Draw everything
// at the scale that was fitted.
void wrap_Minimap_DrawBlocks(CPU *c) {
    uint32_t m = c->r[0];
    if (LD32(m + 0x6f0) != LD32(m + 0x708)) ST32(m + 0x708, LD32(m + 0x6f0));
    f_e89a0_orig(c);
}
