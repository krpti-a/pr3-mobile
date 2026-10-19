// "Experimental": a second single-player entry on the main menu (every part and level unlocked) with its own save file.
//
// The original main menu is a ThumbButtonList built in MainMenu::InitThumbList (Single Player, Online Play, Options,
// More Games). gen.py wraps two original functions (tools/recomp/full.json "wraps"):
//   MainMenu::InitThumbList            -> add a fifth tile "offlinePlayAll" and move it next to Single Player
//   PlatformRacing3::HandleEvent       -> when that tile (event "offlinePlayAll") or the normal one ("offlinePlay") is chosen,
//                                         switch Settings to the matching save file before the original handler runs
// The unlocked profile is created on first use from the game's own default character (Settings::CreateNewCharacter) with
// every hat/head/body/feet owned and enough medals for every level. Both profiles are saved by the game itself
// (Settings::SaveSettings) into Settings.dat / Settings_unlocked.dat.
#include "arm_rt.h"

// original functions (guest addresses)
enum {
    A_ThumbAdd = 0x9d438, A_SaveSettings = 0x63740, A_LoadSettings = 0x63e00, A_CreateNewCharacter = 0x636f4,
    A_cJSON_Parse = 0x29f98, A_cJSON_ReplaceObjectItem = 0xb694,
    // std::vector<RK_SPRITE*> of every hat / head / body / foot (their sizes are the number of parts)
    A_HatVecDat = 0xaa430, A_HatVecOff = 0xaa288, A_HeadVecDat = 0xa9f3c, A_HeadVecOff = 0xa9d94,
    A_BodyVecDat = 0xa9a38, A_BodyVecOff = 0xa9890, A_FootVecDat = 0xa9530, A_FootVecOff = 0xa91f4,
    A_SettingsDat = 0x846c, A_SettingsOff = 0x843c,       // Singleton<Settings> slot, as used by -applicationWillResignActive:
};
void f_94014_orig(CPU *c);
void f_aa8d4_orig(CPU *c);
void f_49aac_orig(CPU *c);

// Calls a guest function from inside another guest call (below the caller's stack pointer).
static uint32_t gcall(CPU *cur, uint32_t addr, const uint32_t *args, int n) {
    if (!cur) { uint32_t ret[2] = {0, 0}; rt_call(addr, (uint32_t *)args, n, ret); return ret[0]; }
    CPU c; memset(&c, 0, sizeof c);
    int extra = n > 4 ? n - 4 : 0;
    uint32_t sp = (cur->sp - 0x1000) & ~7u;
    sp -= 4 * extra; sp &= ~7u;
    for (int i = 0; i < extra; i++) ST32(sp + 4 * i, args[4 + i]);
    for (int i = 0; i < n && i < 4; i++) c.r[i] = args[i];
    c.sp = sp; c.lr = 0x7ffff000u;
    rt_call_indirect(&c, addr);
    return c.r[0];
}

// A std::string (libstdc++ 4.2, copy-on-write) that is never freed: object = pointer to {len, cap, refcount, chars}.
static uint32_t guest_string_data(const char *s) {
    uint32_t n = (uint32_t)strlen(s), p = rt_malloc(12 + n + 1);
    ST32(p, n); ST32(p + 4, n); ST32(p + 8, 0x40000000u); memcpy(rt_ptr(p + 12), s, n + 1);
    return p + 12;
}
static uint32_t guest_string(const char *s) { uint32_t o = rt_malloc(8); ST32(o, guest_string_data(s)); return o; }

static uint32_t settings_object(void) { return LD32(LD32(A_SettingsOff + LD32(A_SettingsDat))); }
static uint32_t vec_count(uint32_t dat, uint32_t off) { uint32_t v = LD32(dat) + off; return (LD32(v + 4) - LD32(v)) / 4; }

// The running game edits a copy of the user (stats, parts, colours, medals) held by the offline socket; the original copies it
// back into Settings with OfflineSocket::SaveToSettings (vtable slot) at some events only (a finished level). Before the
// settings are written the shell does it too, so nothing the player changed is lost. `cur` is NULL outside guest code.
void pr3_sync_user(CPU *cur) {
    enum { A_SocketDat = 0xfc690, A_SocketOff = 0xfc470, A_OfflineSaveToSettings = 0xfbb70 };
    uint32_t mgr = LD32(LD32(LD32(A_SocketDat) + A_SocketOff));
    if (!mgr) return;
    uint32_t sock = LD32(mgr);
    if (!sock) return;
    uint32_t vptr = LD32(sock);
    if (getenv("PR3_DEBUG_MODES")) fprintf(stderr, "[pr3] sync user: manager 0x%x socket 0x%x vptr 0x%x\n", mgr, sock, vptr);
    for (int i = 0; i < 64; i++) {
        if (LD32(vptr + 4 * i) == A_OfflineSaveToSettings) { gcall(cur, A_OfflineSaveToSettings, &sock, 1); return; }
    }
}

// ---- profiles -----------------------------------------------------------------------------------------------------
#define NORMAL_FILE   "Settings.dat"
#define UNLOCKED_FILE "Settings_unlocked.dat"
static int g_mode;                          // 0 = normal profile, 1 = experimental (everything unlocked)
extern char rt_documents_root[1024];

static int file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) { fclose(f); return 1; } return 0; }

static void replace_item(CPU *c, uint32_t json, const char *key, const char *text) {
    uint32_t a[3] = { rt_new_cstr(text), 0, 0 };
    uint32_t item = gcall(c, A_cJSON_Parse, a, 1);
    uint32_t b[3] = { json, rt_new_cstr(key), item };
    gcall(c, A_cJSON_ReplaceObjectItem, b, 3);
}
static void id_range(char *out, size_t cap, uint32_t n) {
    size_t k = 0; k += snprintf(out + k, cap - k, "[");
    for (uint32_t i = 1; i <= n; i++) k += snprintf(out + k, cap - k, "%s%u", i > 1 ? "," : "", i);
    snprintf(out + k, cap - k, "]");
}

// Builds the experimental (all unlocked) profile on top of a fresh default character and writes it to the current Settings path.
static void create_unlocked_profile(CPU *c, uint32_t S) {
    ST32(S + 0x38, 0);                                          // drop the other profile's JSON (leaked: tiny)
    gcall(c, A_CreateNewCharacter, &S, 1);
    uint32_t json = LD32(S + 0x38);
    char buf[4096];
    id_range(buf, sizeof buf, vec_count(A_HatVecDat, A_HatVecOff));  replace_item(c, json, "hatArray", buf);
    id_range(buf, sizeof buf, vec_count(A_HeadVecDat, A_HeadVecOff)); replace_item(c, json, "headArray", buf);
    id_range(buf, sizeof buf, vec_count(A_BodyVecDat, A_BodyVecOff)); replace_item(c, json, "bodyArray", buf);
    id_range(buf, sizeof buf, vec_count(A_FootVecDat, A_FootVecOff)); replace_item(c, json, "feetArray", buf);
    // Levels unlock by the sum of the "medal" values in "campaign" (PR3Socket::CountMyMedals); one entry that is not a
    // real level supplies enough medals for every level without marking any of them as completed.
    replace_item(c, json, "campaign", "{\"0\":{\"timeMS\":0,\"medal\":9999}}");
    if (getenv("PR3_DEBUG_HAT")) { char h[32]; snprintf(h, sizeof h, "%d", atoi(getenv("PR3_DEBUG_HAT"))); replace_item(c, json, "hat", h); }   // test aid
    if (getenv("PR3_DEBUG_PARTS")) { char h[32]; snprintf(h, sizeof h, "%d", atoi(getenv("PR3_DEBUG_PARTS"))); replace_item(c, json, "head", h); replace_item(c, json, "body", h); replace_item(c, json, "feet", h); }   // test aid
    replace_item(c, json, "prizes", "[]");                      // nothing left to win
    gcall(c, A_SaveSettings, &S, 1);
}

// ---- Donkey: an unreleased body part whose art ships in the original data (DonkeyHead/Body/Foot[_Color]) but which
// PartDescriptions::Init never registers. Init fills the name vectors (the names double as titles and file-name prefixes)
// and sizes the sprite caches from them, so after it ran the part is appended to the three name vectors and the six
// sprite-cache vectors are grown to match. Its ids are the next free ones (26) of head, body and feet.
void wrap_PartDescriptions_Init(CPU *c) {
    enum { A_VecStrPushBack = 0xf09c, A_VecSpriteResize = 0x42740 };
    static const uint32_t names[3] = { 0x18a7bc, 0x18a7ec, 0x18a81c };                         // head, body, feet
    static const uint32_t caches[3][2] = { { 0x18a7d4, 0x18a7e0 }, { 0x18a804, 0x18a810 }, { 0x18a834, 0x18a840 } };
    int fresh = (LD32(names[0] + 4) == LD32(names[0]));                                          // Init only does its work once
    f_aa8d4_orig(c);
    if (!fresh || LD32(names[0] + 4) == LD32(names[0])) return;
    uint32_t donkey = guest_string("Donkey");
    for (int i = 0; i < 3; i++) {
        uint32_t a[2] = { names[i], donkey };
        gcall(c, A_VecStrPushBack, a, 2);
        uint32_t n = (LD32(names[i] + 4) - LD32(names[i])) / 4;
        for (int k = 0; k < 2; k++) { uint32_t r[3] = { caches[i][k], n, 0 }; gcall(c, A_VecSpriteResize, r, 3); }
    }
}

static void switch_profile(CPU *c, int mode) {
    if (mode == g_mode) return;
    uint32_t S = settings_object();
    if (!S) return;
    pr3_sync_user(c);
    gcall(c, A_SaveSettings, &S, 1);                            // keep the profile we are leaving
    // The game keeps a relative name ("Settings.dat") and resolves it inside the Documents folder itself.
    const char *name = mode ? UNLOCKED_FILE : NORMAL_FILE;
    char full[1100]; snprintf(full, sizeof full, "%s/%s", rt_documents_root, name);
    ST32(S + 0x34, rt_new_cstr(name));
    g_mode = mode;
    if (file_exists(full)) gcall(c, A_LoadSettings, &S, 1);
    else if (mode) create_unlocked_profile(c, S);
    else { ST32(S + 0x38, 0); gcall(c, A_SaveSettings, &S, 1); }   // writes a fresh default character
    fprintf(stderr, "[pr3] profile: %s\n", mode ? "experimental" : "normal");
}

// ---- hooks (see tools/recomp/full.json "wraps") --------------------------------------------------------------------
void wrap_PlatformRacing3_HandleEvent(CPU *c) {
    uint32_t ev = c->r[1];
    if (ev) {
        uint32_t d = LD32(ev + 4);
        const char *name = (d > 0x1000 && d < RT_STACK_TOP) ? rt_cstr(d) : "";
        if (!strcmp(name, "offlinePlayAll")) {
            uint32_t this_ = c->r[0];
            switch_profile(c, 1);
            c->r[0] = this_; c->r[1] = ev;
            ST32(ev + 4, guest_string_data("offlinePlay"));       // from here on it is the normal single-player event
        } else if (!strcmp(name, "offlinePlay")) {
            uint32_t this_ = c->r[0];
            switch_profile(c, 0);
            c->r[0] = this_; c->r[1] = ev;
        }
    }
    f_49aac_orig(c);
}

static uint32_t g_list;
// the engine stores 2D vectors as doubles (vcvt.f64.f32 of the computed float): x at +0x1c, y at +0x24
static void put_double(uint32_t a, double v) { uint64_t u; memcpy(&u, &v, 8); ST32(a, (uint32_t)u); ST32(a + 4, (uint32_t)(u >> 32)); }
static double get_double(uint32_t a) { uint64_t u = ((uint64_t)LD32(a + 4) << 32) | LD32(a); double v; memcpy(&v, &u, 8); return v; }
void pr3_modes_dump(void) {       // debugging aid
    if (!g_list) return;
    uint32_t begin = LD32(g_list + 0x6c4), end = LD32(g_list + 0x6c8);
    fprintf(stderr, "[pr3] list center=%g cur=%u radius=%g minscale=%g maxscale=%g dx=%g\n", LDF(g_list + 0x6e4), LD32(g_list + 0x6ec), LDF(g_list + 0x6d8), LDF(g_list + 0x6dc), LDF(g_list + 0x6e0), LDF(g_list + 0x6d0));
    for (uint32_t i = 0; i < (end - begin) / 4; i++) { uint32_t b = LD32(begin + 4 * i); fprintf(stderr, "[pr3] now tile %u btn 0x%x x=%g tex=%u vis=%g\n", i, b, get_double(b + 0x1c), LD32(b + 0x6c4 + 0x94), LDF(b + 0x698)); }
}

void wrap_MainMenu_InitThumbList(CPU *c) {
    uint32_t menu = c->r[0], sp = c->sp, lr = c->lr;
    f_94014_orig(c);
    c->sp = sp; c->lr = lr;
    uint32_t list = LD32(menu + 0x14b4);
    g_list = list;
    if (!list) return;
    uint32_t args[7] = { list, guest_string("offlinePlayAll"), rt_new_cstr("SinglePlayerExperimental"), guest_string("Experimental"),
                         guest_string(""), 0, 0 };
    uint32_t made = gcall(c, A_ThumbAdd, args, 7);
    if (getenv("PR3_DEBUG_MODES")) fprintf(stderr, "[pr3] new tile object 0x%x\n", made);
    if (getenv("PR3_DEBUG_MODES")) fprintf(stderr, "[pr3] menu tiles: %u\n", (LD32(list + 0x6c8) - LD32(list + 0x6c4)) / 4);
    // move the new tile (last) to second place and re-space the row
    uint32_t begin = LD32(list + 0x6c4), end = LD32(list + 0x6c8), n = (end - begin) / 4;
    if (n >= 3) {
        uint32_t fresh = LD32(begin + 4 * (n - 1));
        for (uint32_t i = n - 1; i > 1; i--) ST32(begin + 4 * i, LD32(begin + 4 * (i - 1)));
        ST32(begin + 4, fresh);
        float dx = LDF(list + 0x6d0);
        for (uint32_t i = 0; i < n; i++) put_double(LD32(begin + 4 * i) + 0x1c, (double)i * dx);
    }
}
