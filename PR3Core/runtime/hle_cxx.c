// HLE of the libstdc++ 4.2 (32-bit, COW basic_string) pieces the original binary imports.
#include "arm_rt.h"

// guest address of std::string::_Rep::_S_empty_rep_storage (set when dyld binds are applied)
uint32_t rt_empty_rep = 0;
#define EMPTY_DATA (rt_empty_rep + 12)

static uint32_t rep_new(uint32_t len, uint32_t cap) {
    if (cap < len) cap = len;
    uint32_t p = rt_malloc(12 + cap + 1);
    ST32(p, len); ST32(p + 4, cap); ST32(p + 8, 0); ST8(p + 12 + len, 0);
    return p + 12;
}
static void str_release(uint32_t data) {
    if (data == EMPTY_DATA || !data) return;
    int32_t rc = (int32_t)LD32(data - 4);
    if (rc <= 0) rt_free(data - 12); else ST32(data - 4, (uint32_t)(rc - 1));
}
static void str_set_bytes(uint32_t obj, const uint8_t *src, uint32_t n) {
    uint32_t old = LD32(obj);
    uint32_t d;
    if (n == 0) d = EMPTY_DATA;
    else { d = rep_new(n, n); memcpy(rt_ptr(d), src, n); }
    ST32(obj, d);
    str_release(old);
}
static uint32_t str_len(uint32_t obj) { uint32_t d = LD32(obj); return LD32(d - 12); }
static uint8_t *str_bytes(uint32_t obj) { return rt_ptr(LD32(obj)); }

// make the string uniquely owned (copy if shared)
static void str_unshare(uint32_t obj) {
    uint32_t d = LD32(obj);
    if (d == EMPTY_DATA) return;
    int32_t rc = (int32_t)LD32(d - 4);
    if (rc > 0) {
        uint32_t n = LD32(d - 12);
        uint32_t nd = rep_new(n, LD32(d - 8));
        memcpy(rt_ptr(nd), rt_ptr(d), n);
        ST32(d - 4, (uint32_t)(rc - 1));
        ST32(obj, nd);
    }
}

void hle___ZNSsC1EPKcRKSaIcE(CPU *c) {            // basic_string(const char*, const allocator&)
    uint32_t obj = c->r[0]; const char *s = c->r[1] ? rt_cstr(c->r[1]) : "";
    ST32(obj, EMPTY_DATA);
    str_set_bytes(obj, (const uint8_t *)s, (uint32_t)strlen(s));
}
void hle___ZNSsC1ERKSs(CPU *c) {                  // copy constructor (shares the rep)
    uint32_t obj = c->r[0], other = c->r[1];
    uint32_t d = LD32(other);
    if (d == EMPTY_DATA) { ST32(obj, d); return; }
    int32_t rc = (int32_t)LD32(d - 4);
    if (rc < 0) { ST32(obj, EMPTY_DATA); str_set_bytes(obj, rt_ptr(d), LD32(d - 12)); return; }
    ST32(d - 4, (uint32_t)(rc + 1));
    ST32(obj, d);
}
void hle___ZNSsD2Ev(CPU *c) { str_release(LD32(c->r[0])); }
void hle___ZNSs6assignEPKcm(CPU *c) {
    uint32_t obj = c->r[0], n = c->r[2];
    uint8_t *tmp = malloc(n + 1); memcpy(tmp, rt_ptr(c->r[1]), n);
    str_set_bytes(obj, tmp, n); free(tmp);
    c->r[0] = obj;
}
void hle___ZNSs6assignERKSs(CPU *c) {
    uint32_t obj = c->r[0], other = c->r[1];
    if (LD32(obj) == LD32(other)) return;
    uint32_t d = LD32(other);
    str_release(LD32(obj));
    if (d == EMPTY_DATA) { ST32(obj, d); }
    else {
        int32_t rc = (int32_t)LD32(d - 4);
        if (rc < 0) { ST32(obj, EMPTY_DATA); str_set_bytes(obj, rt_ptr(d), LD32(d - 12)); }
        else { ST32(d - 4, (uint32_t)(rc + 1)); ST32(obj, d); }
    }
    c->r[0] = obj;
}
static void str_append(uint32_t obj, const uint8_t *src, uint32_t n) {
    uint32_t len = str_len(obj);
    uint8_t *tmp = malloc(len + n + 1);
    memcpy(tmp, str_bytes(obj), len); memcpy(tmp + len, src, n);
    str_set_bytes(obj, tmp, len + n); free(tmp);
}
void hle___ZNSs6appendEPKcm(CPU *c) { uint8_t *t = malloc(c->r[2] + 1); memcpy(t, rt_ptr(c->r[1]), c->r[2]); str_append(c->r[0], t, c->r[2]); free(t); }
void hle___ZNSs6appendERKSs(CPU *c) { uint32_t o = c->r[1]; uint32_t n = str_len(o); uint8_t *t = malloc(n + 1); memcpy(t, str_bytes(o), n); str_append(c->r[0], t, n); free(t); }
void hle___ZNSs9push_backEc(CPU *c) { uint8_t ch = (uint8_t)c->r[1]; str_append(c->r[0], &ch, 1); }
void hle___ZNSs7reserveEm(CPU *c) { (void)c; }
void hle___ZNSs12_M_leak_hardEv(CPU *c) { str_unshare(c->r[0]); }
void hle___ZNSsixEm(CPU *c) { str_unshare(c->r[0]); c->r[0] = LD32(c->r[0]) + c->r[1]; }
void hle___ZNSs5eraseEmm(CPU *c) {
    uint32_t obj = c->r[0], pos = c->r[1], n = c->r[2], len = str_len(obj);
    if (pos > len) pos = len;
    if (n > len - pos) n = len - pos;
    uint8_t *tmp = malloc(len + 1); memcpy(tmp, str_bytes(obj), len);
    memmove(tmp + pos, tmp + pos + n, len - pos - n);
    str_set_bytes(obj, tmp, len - n); free(tmp);
    c->r[0] = obj;
}
static void str_replace(uint32_t obj, uint32_t pos, uint32_t n1, const uint8_t *s, uint32_t n2) {
    uint32_t len = str_len(obj);
    if (pos > len) pos = len;
    if (n1 > len - pos) n1 = len - pos;
    uint8_t *tmp = malloc(len + n2 + 1);
    memcpy(tmp, str_bytes(obj), pos); memcpy(tmp + pos, s, n2);
    memcpy(tmp + pos + n2, str_bytes(obj) + pos + n1, len - pos - n1);
    str_set_bytes(obj, tmp, len - n1 + n2); free(tmp);
}
void hle___ZNSs7replaceEmmPKc(CPU *c) {
    const char *s = rt_cstr(c->r[3]); uint32_t n = (uint32_t)strlen(s);
    uint8_t *t = malloc(n + 1); memcpy(t, s, n); str_replace(c->r[0], c->r[1], c->r[2], t, n); free(t); c->r[0] = c->r[0];
}
void hle___ZNSs7replaceEmmPKcm(CPU *c) {
    uint32_t n = LD32(c->sp);       // 5th argument on the stack
    uint8_t *t = malloc(n + 1); memcpy(t, rt_ptr(c->r[3]), n); str_replace(c->r[0], c->r[1], c->r[2], t, n); free(t);
}
void hle___ZNKSs4findEPKcm(CPU *c) {
    uint32_t obj = c->r[0], pos = c->r[2]; const char *needle = rt_cstr(c->r[1]);
    uint32_t len = str_len(obj), nl = (uint32_t)strlen(needle);
    c->r[0] = 0xffffffffu;
    if (pos > len) return;
    const char *h = (const char *)str_bytes(obj);
    for (uint32_t i = pos; i + nl <= len; i++) if (!memcmp(h + i, needle, nl)) { c->r[0] = i; return; }
}
void hle___ZNKSs4findEPKcmm(CPU *c) {
    uint32_t obj = c->r[0], pos = c->r[2], nl = c->r[3]; const char *needle = rt_cstr(c->r[1]);
    uint32_t len = str_len(obj);
    c->r[0] = 0xffffffffu;
    if (pos > len) return;
    const char *h = (const char *)str_bytes(obj);
    for (uint32_t i = pos; i + nl <= len; i++) if (!memcmp(h + i, needle, nl)) { c->r[0] = i; return; }
}
void hle___ZNKSs4findEcm(CPU *c) {
    uint32_t obj = c->r[0], pos = c->r[2]; char ch = (char)c->r[1];
    uint32_t len = str_len(obj);
    c->r[0] = 0xffffffffu;
    const char *h = (const char *)str_bytes(obj);
    for (uint32_t i = pos; i < len; i++) if (h[i] == ch) { c->r[0] = i; return; }
}
void hle___ZNKSs6substrEmm(CPU *c) {            // sret: r0 = result, r1 = this, r2 = pos, r3 = n
    uint32_t res = c->r[0], obj = c->r[1], pos = c->r[2], n = c->r[3], len = str_len(obj);
    if (pos > len) pos = len;
    if (n > len - pos) n = len - pos;
    ST32(res, EMPTY_DATA);
    str_set_bytes(res, str_bytes(obj) + pos, n);
}
static int cmp_bytes(const uint8_t *a, uint32_t la, const uint8_t *b, uint32_t lb) {
    uint32_t m = la < lb ? la : lb; int r = memcmp(a, b, m);
    if (r) return r;
    return la == lb ? 0 : (la < lb ? -1 : 1);
}
void hle___ZNKSs7compareEPKc(CPU *c) { const char *s = rt_cstr(c->r[1]); c->r[0] = (uint32_t)cmp_bytes(str_bytes(c->r[0]), str_len(c->r[0]), (const uint8_t *)s, (uint32_t)strlen(s)); }
void hle___ZNKSs7compareERKSs(CPU *c) { c->r[0] = (uint32_t)cmp_bytes(str_bytes(c->r[0]), str_len(c->r[0]), str_bytes(c->r[1]), str_len(c->r[1])); }

// ---- std::list / red-black tree helpers ----------------------------------------------------
void hle___ZNSt15_List_node_base4hookEPS_(CPU *c) {
    uint32_t self = c->r[0], pos = c->r[1];
    ST32(self, pos); uint32_t prev = LD32(pos + 4); ST32(self + 4, prev);
    ST32(prev, self); ST32(pos + 4, self);
}
void hle___ZNSt15_List_node_base6unhookEv(CPU *c) {
    uint32_t self = c->r[0]; uint32_t next = LD32(self), prev = LD32(self + 4);
    ST32(prev, next); ST32(next + 4, prev);
}
#define COLOR(x) LD32(x)
#define PARENT(x) LD32((x) + 4)
#define LEFT(x) LD32((x) + 8)
#define RIGHT(x) LD32((x) + 12)
#define SET_COLOR(x, v) ST32(x, v)
#define SET_PARENT(x, v) ST32((x) + 4, v)
#define SET_LEFT(x, v) ST32((x) + 8, v)
#define SET_RIGHT(x, v) ST32((x) + 12, v)
enum { RED = 0, BLACK = 1 };

void hle___ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base(CPU *c) {
    uint32_t x = c->r[0];
    if (RIGHT(x)) { x = RIGHT(x); while (LEFT(x)) x = LEFT(x); }
    else { uint32_t y = PARENT(x); while (x == RIGHT(y)) { x = y; y = PARENT(y); } if (RIGHT(x) != y) x = y; }
    c->r[0] = x;
}
void hle___ZSt18_Rb_tree_decrementPSt18_Rb_tree_node_base(CPU *c) {
    uint32_t x = c->r[0];
    if (COLOR(x) == RED && PARENT(PARENT(x)) == x) x = RIGHT(x);
    else if (LEFT(x)) { uint32_t y = LEFT(x); while (RIGHT(y)) y = RIGHT(y); x = y; }
    else { uint32_t y = PARENT(x); while (x == LEFT(y)) { x = y; y = PARENT(y); } x = y; }
    c->r[0] = x;
}
static void rotate_left(uint32_t x, uint32_t root_slot) {
    uint32_t y = RIGHT(x);
    SET_RIGHT(x, LEFT(y)); if (LEFT(y)) SET_PARENT(LEFT(y), x);
    SET_PARENT(y, PARENT(x));
    uint32_t root = LD32(root_slot);
    if (x == root) ST32(root_slot, y);
    else if (x == LEFT(PARENT(x))) SET_LEFT(PARENT(x), y); else SET_RIGHT(PARENT(x), y);
    SET_LEFT(y, x); SET_PARENT(x, y);
}
static void rotate_right(uint32_t x, uint32_t root_slot) {
    uint32_t y = LEFT(x);
    SET_LEFT(x, RIGHT(y)); if (RIGHT(y)) SET_PARENT(RIGHT(y), x);
    SET_PARENT(y, PARENT(x));
    uint32_t root = LD32(root_slot);
    if (x == root) ST32(root_slot, y);
    else if (x == RIGHT(PARENT(x))) SET_RIGHT(PARENT(x), y); else SET_LEFT(PARENT(x), y);
    SET_RIGHT(y, x); SET_PARENT(x, y);
}
// header node: color(+0) parent(+4)=root left(+8)=leftmost right(+12)=rightmost ; the root slot is header+4
void hle___ZSt29_Rb_tree_insert_and_rebalancebPSt18_Rb_tree_node_baseS0_RS_(CPU *c) {
    int insert_left = c->r[0] & 0xff; uint32_t x = c->r[1], p = c->r[2], header = c->r[3];
    uint32_t root_slot = header + 4;
    SET_PARENT(x, p); SET_LEFT(x, 0); SET_RIGHT(x, 0); SET_COLOR(x, RED);
    if (insert_left) {
        SET_LEFT(p, x);
        if (p == header) { ST32(header + 4, x); ST32(header + 12, x); }
        else if (p == LD32(header + 8)) ST32(header + 8, x);
    } else {
        SET_RIGHT(p, x);
        if (p == LD32(header + 12)) ST32(header + 12, x);
    }
    while (x != LD32(root_slot) && COLOR(PARENT(x)) == RED) {
        uint32_t xpp = PARENT(PARENT(x));
        if (PARENT(x) == LEFT(xpp)) {
            uint32_t y = RIGHT(xpp);
            if (y && COLOR(y) == RED) { SET_COLOR(PARENT(x), BLACK); SET_COLOR(y, BLACK); SET_COLOR(xpp, RED); x = xpp; }
            else {
                if (x == RIGHT(PARENT(x))) { x = PARENT(x); rotate_left(x, root_slot); }
                SET_COLOR(PARENT(x), BLACK); SET_COLOR(PARENT(PARENT(x)), RED); rotate_right(PARENT(PARENT(x)), root_slot);
            }
        } else {
            uint32_t y = LEFT(xpp);
            if (y && COLOR(y) == RED) { SET_COLOR(PARENT(x), BLACK); SET_COLOR(y, BLACK); SET_COLOR(xpp, RED); x = xpp; }
            else {
                if (x == LEFT(PARENT(x))) { x = PARENT(x); rotate_right(x, root_slot); }
                SET_COLOR(PARENT(x), BLACK); SET_COLOR(PARENT(PARENT(x)), RED); rotate_left(PARENT(PARENT(x)), root_slot);
            }
        }
    }
    SET_COLOR(LD32(root_slot), BLACK);
}
void hle___ZSt28_Rb_tree_rebalance_for_erasePSt18_Rb_tree_node_baseRS_(CPU *c) {
    uint32_t z = c->r[0], header = c->r[1];
    uint32_t root_slot = header + 4, leftmost_slot = header + 8, rightmost_slot = header + 12;
    uint32_t y = z, x = 0, x_parent = 0;
    if (!LEFT(y)) x = RIGHT(y);
    else if (!RIGHT(y)) x = LEFT(y);
    else { y = RIGHT(y); while (LEFT(y)) y = LEFT(y); x = RIGHT(y); }
    if (y != z) {
        SET_PARENT(LEFT(z), y); SET_LEFT(y, LEFT(z));
        if (y != RIGHT(z)) {
            x_parent = PARENT(y);
            if (x) SET_PARENT(x, x_parent);
            SET_LEFT(PARENT(y), x);
            SET_RIGHT(y, RIGHT(z)); SET_PARENT(RIGHT(z), y);
        } else x_parent = y;
        if (LD32(root_slot) == z) ST32(root_slot, y);
        else if (LEFT(PARENT(z)) == z) SET_LEFT(PARENT(z), y); else SET_RIGHT(PARENT(z), y);
        SET_PARENT(y, PARENT(z));
        uint32_t t = COLOR(y); SET_COLOR(y, COLOR(z)); SET_COLOR(z, t);
        y = z;
    } else {
        x_parent = PARENT(y);
        if (x) SET_PARENT(x, PARENT(y));
        if (LD32(root_slot) == z) ST32(root_slot, x);
        else if (LEFT(PARENT(z)) == z) SET_LEFT(PARENT(z), x); else SET_RIGHT(PARENT(z), x);
        if (LD32(leftmost_slot) == z) {
            if (!RIGHT(z)) ST32(leftmost_slot, PARENT(z));
            else { uint32_t m = x; while (LEFT(m)) m = LEFT(m); ST32(leftmost_slot, m); }
        }
        if (LD32(rightmost_slot) == z) {
            if (!LEFT(z)) ST32(rightmost_slot, PARENT(z));
            else { uint32_t m = x; while (RIGHT(m)) m = RIGHT(m); ST32(rightmost_slot, m); }
        }
    }
    if (COLOR(y) != RED) {
        while (x != LD32(root_slot) && (x == 0 || COLOR(x) == BLACK)) {
            if (x == LEFT(x_parent)) {
                uint32_t w = RIGHT(x_parent);
                if (COLOR(w) == RED) { SET_COLOR(w, BLACK); SET_COLOR(x_parent, RED); rotate_left(x_parent, root_slot); w = RIGHT(x_parent); }
                if ((LEFT(w) == 0 || COLOR(LEFT(w)) == BLACK) && (RIGHT(w) == 0 || COLOR(RIGHT(w)) == BLACK)) { SET_COLOR(w, RED); x = x_parent; x_parent = PARENT(x_parent); }
                else {
                    if (RIGHT(w) == 0 || COLOR(RIGHT(w)) == BLACK) { SET_COLOR(LEFT(w), BLACK); SET_COLOR(w, RED); rotate_right(w, root_slot); w = RIGHT(x_parent); }
                    SET_COLOR(w, COLOR(x_parent)); SET_COLOR(x_parent, BLACK);
                    if (RIGHT(w)) SET_COLOR(RIGHT(w), BLACK);
                    rotate_left(x_parent, root_slot); break;
                }
            } else {
                uint32_t w = LEFT(x_parent);
                if (COLOR(w) == RED) { SET_COLOR(w, BLACK); SET_COLOR(x_parent, RED); rotate_right(x_parent, root_slot); w = LEFT(x_parent); }
                if ((RIGHT(w) == 0 || COLOR(RIGHT(w)) == BLACK) && (LEFT(w) == 0 || COLOR(LEFT(w)) == BLACK)) { SET_COLOR(w, RED); x = x_parent; x_parent = PARENT(x_parent); }
                else {
                    if (LEFT(w) == 0 || COLOR(LEFT(w)) == BLACK) { SET_COLOR(RIGHT(w), BLACK); SET_COLOR(w, RED); rotate_left(w, root_slot); w = LEFT(x_parent); }
                    SET_COLOR(w, COLOR(x_parent)); SET_COLOR(x_parent, BLACK);
                    if (LEFT(w)) SET_COLOR(LEFT(w), BLACK);
                    rotate_right(x_parent, root_slot); break;
                }
            }
        }
        if (x) SET_COLOR(x, BLACK);
    }
    c->r[0] = y;
}

void hle___ZNSt8ios_base4InitC1Ev(CPU *c) { (void)c; }
void hle___ZNSt8ios_base4InitD1Ev(CPU *c) { (void)c; }
void hle____cxa_atexit(CPU *c) { c->r[0] = 0; }
void hle___ZSt17__throw_bad_allocv(CPU *c) { (void)c; fprintf(stderr, "[rt] std::bad_alloc thrown\n"); abort(); }
void hle___ZSt20__throw_length_errorPKc(CPU *c) { fprintf(stderr, "[rt] std::length_error: %s\n", rt_cstr(c->r[0])); abort(); }
