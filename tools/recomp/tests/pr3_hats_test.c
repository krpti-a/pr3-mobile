// Standalone Santa hat runtime regression test.
//
// Build from the repository root with:
//   cc -std=c11 -Wall -Wextra -Werror -I PR3Core/include \
//      tools/recomp/tests/pr3_hats_test.c PR3Core/runtime/pr3_hats.c \
//      -o /tmp/pr3_hats_test && /tmp/pr3_hats_test
//
// The test links the runtime against small native-function shims. It checks
// the behavior documented by the HTML5 implementation: a landed-on block
// temporarily uses the real ice block settings and gets an ice block sprite
// overlay, which fades out after 50 frames.

#include "arm_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TEST_HEAP_SIZE = 1 << 20,
    TEST_STACK_SIZE = 1 << 16,
    MANAGER_GLOBAL = 0x11e670,
    ICE_SPRITE_ID = 109,
    FADE_FRAMES = 20,
};

uint8_t *rt_img;
uint8_t *rt_heap;
uint8_t *rt_stack;
uint8_t rt_scratch[16];
uint32_t rt_fault_count;
uint32_t rt_watch_lo = 0xffffffffu;
uint32_t rt_watch_len;

static uint32_t heap_next;
static uint32_t freed[128];
static uint32_t freed_count;
static int in_level;
static int freeze_orig_calls;
static int dtor_orig_calls;
static int get_vars_calls;
static uint32_t get_vars_block;
static int request_block_calls;
static uint32_t request_manager;
static uint32_t request_id;
static uint32_t ice_settings;
static uint32_t ice_block;
static uint32_t ice_sprite;
static int set_pos_calls;
static uint32_t pos_sprite, pos_x, pos_y, pos_z;
static int set_scale_calls;
static uint32_t scale_sprite, scale_x, scale_y;
static int set_color_calls;
static uint32_t color_sprite, color_r, color_g, color_b, color_a;
static int draw_calls;
static uint32_t draw_sprite;

static void fail(const char *expr, const char *file, int line) {
    fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
    exit(1);
}

#define CHECK(expr) do { if (!(expr)) fail(#expr, __FILE__, __LINE__); } while (0)

void rt_fault(uint32_t addr, int write) {
    (void)write;
    fprintf(stderr, "guest memory fault at 0x%x\n", addr);
    rt_fault_count++;
    abort();
}

void rt_watch_hit(uint32_t addr, uint32_t value, int size) {
    (void)addr; (void)value; (void)size;
}

uint32_t rt_malloc(uint32_t size) {
    if (!size) size = 1;
    size = (size + 7u) & ~7u;
    CHECK(heap_next + size < TEST_HEAP_SIZE);
    uint32_t p = RT_HEAP_BASE + heap_next;
    heap_next += size;
    return p;
}

uint32_t rt_alloc(uint32_t size) { return rt_malloc(size); }

void rt_free(uint32_t p) {
    CHECK(freed_count < sizeof(freed) / sizeof(freed[0]));
    freed[freed_count++] = p;
}

uint32_t rt_usable_size(uint32_t p) { (void)p; return 0; }
int rt_in_level(void) { return in_level; }

// Original methods called by the wrappers.
void f_399c4_orig(CPU *c) { (void)c; freeze_orig_calls++; }
void f_3ae88_orig(CPU *c) { (void)c; dtor_orig_calls++; }

// Block::GetVars(). A frozen block must use this pointer directly; the
// production runtime must not copy or mutate the block's own settings.
void f_39dfc(CPU *c) {
    get_vars_calls++;
    get_vars_block = c->r[0];
    c->r[0] = ice_settings;
}

// BlockManager::RequestBlock(int). The singleton is deliberately exposed in
// guest memory so the test catches callers that use a different manager.
void f_3ede8(CPU *c) {
    request_block_calls++;
    request_manager = c->r[0];
    request_id = c->r[1];
    c->r[0] = ice_block;
}

// Native render calls used by the overlay. Their argument layouts mirror the
// generated functions: SetPos(this,x,y,z), SetScale(this,x,y),
// SetColor(this,r,g,b,a-on-stack), and Draw(this,...).
void f_f6b8(CPU *c) {
    set_pos_calls++;
    pos_sprite = c->r[0]; pos_x = c->r[1]; pos_y = c->r[2]; pos_z = c->r[3];
}

void f_f760(CPU *c) {
    set_scale_calls++;
    scale_sprite = c->r[0]; scale_x = c->r[1]; scale_y = c->r[2];
}

void f_f738(CPU *c) {
    set_color_calls++;
    color_sprite = c->r[0]; color_r = c->r[1]; color_g = c->r[2];
    color_b = c->r[3]; color_a = c->sp ? LD32(c->sp) : 0;
}

// Older generated trees expose this symbol as the wrapper's original name.
// Keep both shims so the test remains useful while generated files are rebuilt.
void f_f738_orig(CPU *c) { f_f738(c); }

void f_104d0(CPU *c) { draw_calls++; draw_sprite = c->r[0]; }

void pr3_hats_tick(void);
void wrap_Block_Freeze(CPU *c);
void wrap_Block_dtor(CPU *c);
void pr3_hats_draw_overlay(CPU *c, uint32_t block, uint32_t x_bits,
                           uint32_t y_bits, uint32_t scale_bits);

static void begin_case(void) {
    // End any previous case before recycling guest addresses.
    in_level = 0;
    pr3_hats_tick();
    heap_next = 0x1000;
    freed_count = 0;
    memset(freed, 0, sizeof freed);
    freeze_orig_calls = 0;
    dtor_orig_calls = 0;
    get_vars_calls = 0;
    get_vars_block = 0;
    request_block_calls = 0;
    request_manager = 0;
    request_id = 0;
    set_pos_calls = 0;
    set_scale_calls = 0;
    set_color_calls = 0;
    draw_calls = 0;
    pos_sprite = pos_x = pos_y = pos_z = 0;
    scale_sprite = scale_x = scale_y = 0;
    color_sprite = color_r = color_g = color_b = color_a = 0;
    draw_sprite = 0;
    in_level = 1;
}

static uint32_t new_block(uint32_t settings, uint32_t sprite) {
    uint32_t block = rt_malloc(0x100);
    memset(rt_ptr(block), 0, 0x100);
    ST32(block, sprite);
    ST32(block + 0x10, BITS_F32(1.0f)); // block opacity used by the overlay
    ST32(block + 0x1c, settings);
    return block;
}

static uint32_t new_object(void) {
    uint32_t p = rt_malloc(16);
    memset(rt_ptr(p), 0, 16);
    return p;
}

static void setup_ice_objects(uint32_t manager) {
    ice_settings = new_object();
    ice_sprite = new_object();
    ice_block = new_block(ice_settings, ice_sprite);
    ST32(MANAGER_GLOBAL, manager);
    // The native singleton is a pointer to a manager object; RequestBlock's
    // this pointer is the first word loaded from that object.
    ST32(manager, manager);
}

static void freeze_one(uint32_t block) {
    CPU c = {0};
    c.r[0] = block;
    wrap_Block_Freeze(&c);
}

static void draw_one(uint32_t block, float x, float y, float scale) {
    CPU c = {0};
    c.sp = RT_STACK_TOP - 0x100;
    pr3_hats_draw_overlay(&c, block, BITS_F32(x), BITS_F32(y), BITS_F32(scale));
}

static void test_freezes_only_one_block_and_uses_real_ice_vars(void) {
    begin_case();
    uint32_t manager = new_object();
    uint32_t own_settings = new_object();
    uint32_t other_settings = new_object();
    uint32_t block = new_block(own_settings, new_object());
    uint32_t other = new_block(other_settings, new_object());
    setup_ice_objects(manager);

    freeze_one(block);
    CHECK(freeze_orig_calls == 1);
    CHECK(get_vars_calls == 1 && get_vars_block == block);
    CHECK(LD32(block + 0x1c) == ice_settings);
    CHECK(LD32(other + 0x1c) == other_settings);
    CHECK(request_block_calls == 1);
    CHECK(request_manager == manager && request_id == ICE_SPRITE_ID);

    // Refreshing the same block restarts the timer but does not create a
    // second effect object or touch the neighboring block.
    for (int i = 0; i < 40; i++) pr3_hats_tick();
    freeze_one(block);
    CHECK(freeze_orig_calls == 2);
    CHECK(get_vars_calls == 1 && request_block_calls == 1);
    CHECK(LD32(block + 0x1c) == ice_settings);
    CHECK(LD32(other + 0x1c) == other_settings);
}

static void test_ice_overlay_is_positioned_white_and_fades(void) {
    begin_case();
    uint32_t manager = new_object();
    uint32_t own_settings = new_object();
    uint32_t block = new_block(own_settings, new_object());
    setup_ice_objects(manager);
    freeze_one(block);

    // The first 30 rendered frames remain fully opaque. The native frame
    // callback decrements the timer before BlockMap::Render draws.
    for (int frame = 0; frame < 30; frame++) {
        pr3_hats_tick();
        draw_one(block, 12.5f, -4.0f, 1.25f);
        CHECK(draw_calls == frame + 1);
        CHECK(draw_sprite == ice_sprite);
        CHECK(pos_sprite == ice_sprite && pos_x == BITS_F32(12.5f) && pos_y == BITS_F32(-4.0f));
        CHECK(scale_sprite == ice_sprite && scale_x == BITS_F32(1.25f) && scale_y == BITS_F32(1.25f));
        CHECK(color_sprite == ice_sprite);
        CHECK(color_r == BITS_F32(1.0f) && color_g == BITS_F32(1.0f) && color_b == BITS_F32(1.0f));
        CHECK(color_a == BITS_F32(1.0f));
    }

    // The remaining visible frames fade from 0.95 to 0.05 in 0.05 steps;
    // the 20th timer step removes the overlay entirely.
    for (int frame = 0; frame < FADE_FRAMES - 1; frame++) {
        pr3_hats_tick();
        draw_one(block, 12.5f, -4.0f, 1.25f);
        uint32_t expected_alpha = BITS_F32((float)(FADE_FRAMES - frame - 1) / (float)FADE_FRAMES);
        CHECK(color_a == expected_alpha);
    }
    pr3_hats_tick();
    CHECK(LD32(block + 0x1c) == own_settings);
    int draws_at_expiry = draw_calls;
    draw_one(block, 12.5f, -4.0f, 1.25f);
    CHECK(draw_calls == draws_at_expiry);
}

static void test_dtor_and_level_exit_restore_without_tinting_unrelated(void) {
    begin_case();
    uint32_t manager = new_object();
    uint32_t own_settings = new_object();
    uint32_t other_settings = new_object();
    uint32_t block = new_block(own_settings, new_object());
    uint32_t other = new_block(other_settings, new_object());
    setup_ice_objects(manager);
    freeze_one(block);
    draw_one(other, 1.0f, 2.0f, 1.0f);
    CHECK(draw_calls == 0);
    CHECK(LD32(other + 0x1c) == other_settings);
    CHECK(set_color_calls == 0); // no global SetColor interception/tint

    CPU c = {0};
    c.r[0] = block;
    wrap_Block_dtor(&c);
    CHECK(dtor_orig_calls == 1);
    CHECK(LD32(block + 0x1c) == own_settings);
    draw_one(block, 1.0f, 2.0f, 1.0f);
    CHECK(draw_calls == 0);

    // A second block survives a level exit and is restored by the next tick.
    uint32_t block2 = new_block(other_settings, new_object());
    freeze_one(block2);
    CHECK(LD32(block2 + 0x1c) == ice_settings);
    in_level = 0;
    pr3_hats_tick();
    CHECK(LD32(block2 + 0x1c) == other_settings);
    draw_one(block2, 1.0f, 2.0f, 1.0f);
    CHECK(draw_calls == 0);
    CHECK(freed_count == 0); // RequestBlock returns a cached game object; no overlay ownership leak.
}

int main(void) {
    rt_img = calloc(1, RT_IMG_SIZE);
    rt_heap = calloc(1, TEST_HEAP_SIZE);
    rt_stack = calloc(1, TEST_STACK_SIZE);
    CHECK(rt_img && rt_heap && rt_stack);

    test_freezes_only_one_block_and_uses_real_ice_vars();
    test_ice_overlay_is_positioned_white_and_fades();
    test_dtor_and_level_exit_restore_without_tinting_unrelated();

    free(rt_img);
    free(rt_heap);
    free(rt_stack);
    puts("pr3_hats_test: all tests passed");
    return 0;
}
