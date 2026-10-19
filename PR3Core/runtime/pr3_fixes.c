// Fixes for defects of the original app that the faithful port would otherwise reproduce.
// Wrapped original functions are listed under "wraps" in tools/recomp/full.json (see gen.py).
#include "arm_rt.h"

void f_363dc_orig(CPU *c);

// Player::ShowHats() re-applies the hat colours only when it also reloads a hat sprite (dirty flags at this+0x16b..0x171),
// so a changed hat colour showed up only after switching to another hat. Mark every slot dirty so the sprites (and their
// colours) are rebuilt whenever the appearance is refreshed.
void wrap_Player_ShowHats(CPU *c) {
    uint32_t player = c->r[0];
    for (int i = 0; i < 7; i++) ST8(player + 0x16b + i, 1);
    f_363dc_orig(c);
}
