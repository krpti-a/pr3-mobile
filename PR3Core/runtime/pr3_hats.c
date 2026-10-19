// Santa Hat: the original collision path calls Block::Freeze on the one block
// the player lands on. Its ice settings are present, but the animation and ice
// graphic are never scheduled by the mobile binary.
#include "arm_rt.h"

void f_399c4_orig(CPU *c);
void f_3ae88_orig(CPU *c);
void f_39dfc(CPU *c);  // Block::GetVars (returns ice settings while frozen)
void f_3ede8(CPU *c);  // BlockManager::RequestBlock
void f_f738(CPU *c);   // RK_OBJECT::SetColor
void f_f6b8(CPU *c);   // RK_OBJECT::SetPos
void f_f760(CPU *c);   // RK_OBJECT::SetScale
void f_104d0(CPU *c);  // RK_SPRITE::Draw
int rt_in_level(void);

// Block::GetVars reads this singleton slot (literal at 0x39e24 + 0xe484c).
enum { FREEZE_FRAMES = 50, MAX_FROZEN = 64, BLOCK_MANAGER_GLOBAL = 0x11e670 };
typedef struct {
    uint32_t block;
    uint32_t original_vars;
    uint32_t ice_vars;
    uint32_t ice_block;
    int frames_left;
} FrozenBlock;
static FrozenBlock frozen_blocks[MAX_FROZEN];
static int frozen_count;

static int find_block(uint32_t block) {
    for (int i = 0; i < frozen_count; i++)
        if (frozen_blocks[i].block == block) return i;
    return -1;
}

static void thaw(int i) {
    FrozenBlock entry = frozen_blocks[i];
    if (LD32(entry.block + 0x1c) == entry.ice_vars)
        ST32(entry.block + 0x1c, entry.original_vars);
    ST8(entry.block + 0xe4, 0);
    ST32(entry.block + 0xe0, 0);
    ST32(entry.block + 0xe8, 0);
    frozen_blocks[i] = frozen_blocks[--frozen_count];
}

void wrap_Block_Freeze(CPU *c) {
    uint32_t block = c->r[0];
    int i = find_block(block);
    uint32_t original_vars = i >= 0 ? frozen_blocks[i].original_vars : LD32(block + 0x1c);
    f_399c4_orig(c);
    if (i >= 0) {
        frozen_blocks[i].frames_left = FREEZE_FRAMES;
        return;
    }
    if (frozen_count == MAX_FROZEN || !original_vars) {
        ST8(block + 0xe4, 0);
        ST32(block + 0xe0, 0);
        return;
    }

    CPU call = *c;
    call.r[0] = block;
    f_39dfc(&call);
    uint32_t ice_vars = call.r[0];
    if (!ice_vars) {
        ST8(block + 0xe4, 0);
        ST32(block + 0xe0, 0);
        return;
    }

    // The same ice block asset used by the HTML5 client (ID 109) provides the
    // sprite. ID 9 supplies its physics through Block::GetVars above.
    uint32_t manager_global = LD32(BLOCK_MANAGER_GLOBAL);
    uint32_t manager = manager_global ? LD32(manager_global) : 0;
    uint32_t ice_block = 0;
    if (manager) {
        call = *c;
        call.r[0] = manager;
        call.r[1] = 109;
        f_3ede8(&call);
        ice_block = call.r[0];
    }

    ST32(block + 0x1c, ice_vars);
    frozen_blocks[frozen_count++] = (FrozenBlock){block, original_vars, ice_vars, ice_block, FREEZE_FRAMES};
}

void wrap_Block_dtor(CPU *c) {
    int i = find_block(c->r[0]);
    if (i >= 0) thaw(i);
    f_3ae88_orig(c);
}

void pr3_hats_tick(void) {
    if (!frozen_count) return;
    if (!rt_in_level()) {
        while (frozen_count) thaw(frozen_count - 1);
        return;
    }
    for (int i = 0; i < frozen_count; ) {
        FrozenBlock *entry = &frozen_blocks[i];
        entry->frames_left--;
        ST32(entry->block + 0xe0, (uint32_t)entry->frames_left);
        if (entry->frames_left <= 0) thaw(i);
        else i++;
    }
}

// Called from BlockMap::Render immediately after drawing the actual block,
// while the map camera transform is active. This draws only that block's ice
// sprite on top and fades it over the final 20 frames.
void pr3_hats_draw_overlay(CPU *c, uint32_t block, uint32_t x, uint32_t y, uint32_t scale) {
    int i = find_block(block);
    if (i < 0) return;
    FrozenBlock *entry = &frozen_blocks[i];
    if (!entry->ice_block) return;
    uint32_t sprite = LD32(entry->ice_block);
    if (!sprite) return;

    float alpha = entry->frames_left >= 20 ? 1.0f : (float)entry->frames_left / 20.0f;
    alpha *= F32_BITS(LD32(block + 0x10));
    CPU call = *c;
    call.sp -= 4;
    ST32(call.sp, BITS_F32(alpha));
    call.r[0] = sprite;
    call.r[1] = BITS_F32(1.0f);
    call.r[2] = BITS_F32(1.0f);
    call.r[3] = BITS_F32(1.0f);
    f_f738(&call);

    call = *c;
    call.r[0] = sprite;
    call.r[1] = x;
    call.r[2] = y;
    call.r[3] = 0;
    f_f6b8(&call);

    call = *c;
    call.r[0] = sprite;
    call.r[1] = scale;
    call.r[2] = scale;
    f_f760(&call);

    call = *c;
    call.r[0] = sprite;
    call.r[1] = 1;
    call.r[2] = 0;
    call.r[3] = 0;
    f_104d0(&call);
}
