// Fixes for defects of the original app that the faithful port would otherwise reproduce.
// Wrapped original functions are listed under "wraps" in tools/recomp/full.json (see gen.py).
#include "arm_rt.h"

void f_363dc_orig(CPU *c);
void f_e89a0_orig(CPU *c);
void f_548c0_orig(CPU *c);
extern char rt_resource_root[1024];

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

// The selector and race renderer place parts differently. The build creates
// exact pixel-shifted _Game PNG variants, which the racer draws in place of
// the selector's sprites without changing any draw coordinates.
static const char *const game_head[26] = {
    [21] = "BoshHead", [23] = "CranberryHead",
    [24] = "PenguinHead", [25] = "ReindeerHead"
};
static const char *const game_body[26] = {
    [24] = "PenguinBody", [25] = "ReindeerBody"
};
typedef struct { uint32_t base, color; int attempted; } GamePart;
static GamePart loaded_heads[26], loaded_bodies[26];

static uint32_t part_call(CPU *cur, uint32_t addr, const uint32_t *args, int n) {
    CPU call = {0};
    int extra = n > 4 ? n - 4 : 0;
    uint32_t sp = (cur->sp - 0x1000 - 4 * extra) & ~7u;
    for (int i = 0; i < extra; i++) ST32(sp + 4 * i, args[4 + i]);
    for (int i = 0; i < n && i < 4; i++) call.r[i] = args[i];
    call.sp = sp; call.lr = 0x7ffff000u;
    rt_call_indirect(&call, addr);
    return call.r[0];
}

static uint32_t load_part_sprite(CPU *c, const char *filename) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", rt_resource_root, filename);
    uint32_t sprite = rt_malloc(0x100);
    uint32_t ctor[] = {sprite};
    part_call(c, 0x116f0, ctor, 1); // RK_SPRITE::RK_SPRITE()
    uint32_t load[] = {sprite, rt_new_cstr(path), 1, 0, 0, 0, 0, 1};
    uint32_t loaded = part_call(c, 0x113bc, load, 8); // RK_SPRITE::Load(...)
    if (!loaded || !LD8(sprite + 156)) {
        fprintf(stderr, "[pr3] could not load part sprite %s\n", path);
        return 0;
    }
    return sprite;
}

static GamePart *get_game_part(CPU *c, GamePart *cache, uint32_t id, const char *name) {
    GamePart *part = &cache[id];
    if (!part->attempted) {
        part->attempted = 1;
        char filename[80];
        snprintf(filename, sizeof filename, "%s_Game.png", name);
        part->base = load_part_sprite(c, filename);
        snprintf(filename, sizeof filename, "%s_Game_Color.png", name);
        part->color = load_part_sprite(c, filename);
    }
    return part;
}

static void copy_sprite_color(uint32_t source, uint32_t target) {
    if (!source || !target) return;
    for (uint32_t offset = 64; offset <= 76; offset += 4)
        ST32(target + offset, LD32(source + offset));
}

void wrap_ActivePlayer_UpdateAnimation(CPU *c) {
    uint32_t player = c->r[0];
    uint32_t head = LD32(player + 204), body = LD32(player + 208);
    uint32_t original_base_head = LD32(player + 92), original_color_head = LD32(player + 96);
    uint32_t original_base_body = LD32(player + 100), original_color_body = LD32(player + 104);
    if (head < 26 && game_head[head] && original_base_head && original_color_head) {
        GamePart *part = get_game_part(c, loaded_heads, head, game_head[head]);
        if (part->base && part->color) {
            copy_sprite_color(original_base_head, part->base);
            copy_sprite_color(original_color_head, part->color);
            ST32(player + 92, part->base);
            ST32(player + 96, part->color);
        }
    }
    if (body < 26 && game_body[body] && original_base_body && original_color_body) {
        GamePart *part = get_game_part(c, loaded_bodies, body, game_body[body]);
        if (part->base && part->color) {
            copy_sprite_color(original_base_body, part->base);
            copy_sprite_color(original_color_body, part->color);
            ST32(player + 100, part->base);
            ST32(player + 104, part->color);
        }
    }
    f_548c0_orig(c);
    ST32(player + 92, original_base_head);
    ST32(player + 96, original_color_head);
    ST32(player + 100, original_base_body);
    ST32(player + 104, original_color_body);
}
