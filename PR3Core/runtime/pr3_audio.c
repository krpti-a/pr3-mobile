// Sound (main branch). Diagnostics: PR3_SFXLOG=1 prints every sound effect created and played (file name).
// Additions: the original ships SproingCartoon.wav but no code ever plays it - bounce blocks were silent. It is played when a
// bounce block (side type 3) throws the player back.
#include "arm_rt.h"

void f_1fd0c_orig(CPU *c);
void f_1ff28_orig(CPU *c);
void f_19dcc_orig(CPU *c);
void f_578f8_orig(CPU *c);

static char names[512][48]; static uint32_t objs[512]; static int nobj;
static const char *name_of(uint32_t o) { for (int i = 0; i < nobj; i++) if (objs[i] == o) return names[i]; return "?"; }

// RK_SFX::RK_SFX(const char *file, bool)
void wrap_RK_SFX_ctor(CPU *c) {
    uint32_t o = c->r[0], file = c->r[1];
    f_1fd0c_orig(c);
    if (getenv("PR3_SFXLOG") && nobj < 512) { objs[nobj] = o; snprintf(names[nobj], 48, "%s", rt_cstr(file)); nobj++; }
}
// RK_SFX::Play(): r0 = sfx
void wrap_RK_SFX_Play(CPU *c) {
    if (getenv("PR3_SFXLOG")) fprintf(stderr, "[sfx] play %s%s\n", name_of(c->r[0]), LD8(0x1881b0 + 0) ? "" : " (sound disabled)");
    f_1ff28_orig(c);
}

// PlayGameSFX(id): the game's own effect table
void wrap_PlayGameSFX(CPU *c) {
    uint32_t id = c->r[0];
    static int dumped; if (getenv("PR3_SFXLOG") && !dumped++) for (uint32_t i = 0; i <= 0x23; i++) fprintf(stderr, "[sfx] table %u = %s\n", i, name_of(LD32(LD32(0x19df4) + 0x19dec + i * 4)));
    if (getenv("PR3_SFXLOG") && id <= 0x23) fprintf(stderr, "[sfx] PlayGameSFX(%u) -> %s\n", id, name_of(LD32(LD32(0x19df4) + 0x19dec + id * 4)));
    f_19dcc_orig(c);
}



// ActivePlayer::TouchBlock(Block *block, int side): the bounce block ("trampoline") has side type 3 on all four sides (see
// BlockManager::CreateDefaultBlocks). The original plays nothing when it throws the player back. Play the unused SproingCartoon.wav
// (at most every 10 frames, and only while the player is actually moving).
static uint32_t gcall(CPU *cur, uint32_t addr, const uint32_t *args, int n) {
    CPU t; memset(&t, 0, sizeof t);
    for (int i = 0; i < n && i < 4; i++) t.r[i] = args[i];
    t.sp = (cur->sp - 0x1000) & ~7u; t.lr = 0x7ffff000u;
    rt_call_indirect(&t, addr);
    return t.r[0];
}
void wrap_ActivePlayer_TouchBlock(CPU *c) {
    static uint32_t last_frame_block, cooldown;
    uint32_t player = c->r[0], block = c->r[1], side = c->r[2];
    f_578f8_orig(c);
    if (cooldown) cooldown--;
    uint32_t a[3] = { player, side, LD32(player + 0x88) };
    uint32_t idx = side == 4 ? 4 : gcall(c, 0x544b4, a, 3);             // AdjustIndexByRotation
    uint32_t b[2] = { LD32(block + 0x1c), idx };
    uint32_t ptr = gcall(c, 0x3bdb0, b, 2);                             // BlockSettings::GetSideSettings(int)
    int bounce = ptr && LD32(ptr) == 3;
    if (getenv("PR3_SFXLOG") && bounce) fprintf(stderr, "[sfx] bounce block touched (side %u, cooldown %u)\n", side, cooldown);
    if (bounce && !cooldown) { uint32_t id = 23; gcall(c, 0x19dcc, &id, 1); cooldown = 10; }      // PlayGameSFX(SproingCartoon)
    (void)last_frame_block;
}
