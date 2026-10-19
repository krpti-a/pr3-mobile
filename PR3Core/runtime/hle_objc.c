// Minimal Objective-C object model for the recompiled game. Classes and objects are small guest blocks;
// messages are dispatched by selector name. Only the Foundation/UIKit pieces the game really uses are implemented.
#include "arm_rt.h"
#include <ctype.h>
#include <sys/time.h>

// HLE objects (NSString, NSDate, NSTimer, ...) are host-side records addressed by opaque handles in 0x30000000..0x3fffffff
// (slot index + generation, so a stale handle never aliases a newer object). They are reference counted like the real
// objects: alloc/factories return +1, retain/release adjust it, and an autorelease pool is drained once per frame.
#define HANDLE_BASE  0x30000000u
#define HANDLE_SLOTS (1 << 17)
#define IMMORTAL     (1 << 28)
typedef struct { uint32_t addr; char cls[40]; int is_class; char *str; uint32_t *items; int nitems; double t; int rc; uint8_t live, gen; int next_free; } Obj;
static Obj *objs; static int nslots, free_head = -1;
// Classes, bound data symbols (kCFAllocatorDefault, ...) and singletons live in real guest memory (the game dereferences the
// data symbols) and are never freed.
#define MAX_STATIC 8192
static Obj *statics; static int nstatics; static int16_t static_hash[MAX_STATIC * 2];
static uint32_t *pool; static int npool, cappool;
static uint32_t *pool_cs; static int npool_cs, cap_cs;      // guest C strings handed out by -UTF8String (valid until the pool drains)

extern char rt_resource_root[1024];
extern char rt_documents_root[1024];
extern uint32_t rt_empty_rep;

void (*rt_shell_start_update)(void) = NULL;
void (*rt_shell_end_update)(void) = NULL;
void (*rt_shell_hook)(const char *sel, uint32_t self, CPU *c) = NULL;

static Obj *obj_for(uint32_t a) {
    uint32_t off = a - HANDLE_BASE;
    if (a < HANDLE_BASE || off >= (1u << 28)) {
        if (!statics) return NULL;
        for (uint32_t h = (a >> 4) & (MAX_STATIC * 2 - 1); static_hash[h]; h = (h + 1) & (MAX_STATIC * 2 - 1))
            if (statics[static_hash[h] - 1].addr == a) return &statics[static_hash[h] - 1];
        return NULL;
    }
    if (!objs) return NULL;
    uint32_t idx = (off >> 4) & (HANDLE_SLOTS - 1);
    if (idx >= (uint32_t)nslots) return NULL;
    Obj *o = &objs[idx];
    return (o->live && o->addr == a) ? o : NULL;
}
static Obj *obj_new(const char *cls, int is_class) {
    if (!objs) objs = calloc(HANDLE_SLOTS, sizeof(Obj));
    int idx;
    if (free_head >= 0) { idx = free_head; free_head = objs[idx].next_free; }
    else if (nslots < HANDLE_SLOTS) idx = nslots++;
    else { fprintf(stderr, "[objc] object table full\n"); idx = HANDLE_SLOTS - 1; }
    Obj *o = &objs[idx]; uint8_t gen = (uint8_t)((o->gen + 1) & 0x7f);
    memset(o, 0, sizeof *o);
    o->gen = gen; o->live = 1; o->rc = 1;
    o->addr = HANDLE_BASE + ((uint32_t)gen << 21) + ((uint32_t)idx << 4);
    strncpy(o->cls, cls, 39); o->is_class = is_class;
    return o;
}
static void rc_release(uint32_t a);
static void timer_forget(uint32_t h);
static void obj_free(Obj *o) {
    if (!strcmp(o->cls, "NSTimer")) timer_forget(o->addr);
    free(o->str);
    for (int i = 0; i < o->nitems; i++) rc_release(o->items[i]);
    free(o->items);
    int idx = (int)(o - objs); uint8_t gen = o->gen;
    memset(o, 0, sizeof *o); o->gen = gen; o->next_free = free_head; free_head = idx;
}
static Obj *obj_new_static(const char *cls, int is_class) {
    if (!statics) statics = calloc(MAX_STATIC, sizeof(Obj));
    if (nstatics >= MAX_STATIC) { fprintf(stderr, "[objc] static object table full\n"); return &statics[MAX_STATIC - 1]; }
    Obj *o = &statics[nstatics++];
    o->live = 1; o->rc = IMMORTAL; o->addr = rt_alloc(16); strncpy(o->cls, cls, 39); o->is_class = is_class;
    uint32_t h = (o->addr >> 4) & (MAX_STATIC * 2 - 1);
    while (static_hash[h]) h = (h + 1) & (MAX_STATIC * 2 - 1);
    static_hash[h] = (int16_t)nstatics;
    return o;
}
uint32_t hle_objc_new_object(const char *cls);
static Obj *class_obj(const char *name) {
    for (int i = 0; i < nstatics; i++) if (statics[i].is_class && !strcmp(statics[i].cls, name)) return &statics[i];
    return obj_new_static(name, 1);
}

uint32_t hle_objc_new_object(const char *cls) { return obj_new_static(cls, 0)->addr; }

void hle_objc_bind(uint32_t slot, const char *sym) {
    uint32_t v;
    if (!strncmp(sym, "_OBJC_CLASS_$_", 14)) v = class_obj(sym + 14)->addr;
    else if (!strncmp(sym, "_OBJC_METACLASS_$_", 18)) { Obj *o = obj_new_static(sym + 18, 1); v = o->addr; }
    else if (!strcmp(sym, "___CFConstantStringClassReference")) v = class_obj("__NSCFConstantString")->addr;
    else if (!strcmp(sym, "___stack_chk_guard")) v = 0xdeadbeef;
    else if (!strcmp(sym, "__DefaultRuneLocale")) { extern uint32_t rt_make_rune_locale(void); ST32(slot, rt_make_rune_locale()); return; }
    else if (!strcmp(sym, "__ZNSs4_Rep20_S_empty_rep_storageE")) { rt_empty_rep = rt_alloc(64); ST32(slot, rt_empty_rep); return; }
    else {
        // some "data" binds are really function pointers (operator new/delete, ...): point them at the import stub
        for (uint32_t i = 0; i < rt_stub_name_count; i++) if (!strcmp(rt_stub_names[i].name, sym)) { ST32(slot, rt_stub_names[i].addr); return; }
        Obj *o = obj_new_static(sym, 0); v = o->addr;
    }
    ST32(slot, v);
}

// ---- app-local ObjC classes (compiled into the original binary; their methods run as recompiled code) ----
typedef struct { uint32_t cls; char name[48]; } ImgClass;
static ImgClass img_classes[64]; static int nimg;
static int is_image_class(uint32_t a) { for (int i = 0; i < nimg; i++) if (img_classes[i].cls == a) return 1; return 0; }
static uint32_t ro_of(uint32_t cls) { return LD32(cls + 16) & ~3u; }

void hle_objc_register_image_classes(void) {
    for (uint32_t a = 0x11e8d0; a < 0x11e978; a += 4) {
        uint32_t cls = LD32(a);
        if (nimg >= 64) break;
        img_classes[nimg].cls = cls;
        strncpy(img_classes[nimg].name, rt_cstr(LD32(ro_of(cls) + 16)), 47);
        // the metaclass (isa) is also an image struct
        uint32_t meta = LD32(cls);
        (void)meta;
        nimg++;
    }
}

// Find the IMP for a selector in a class (walking image superclasses). Works for metaclasses too.
static uint32_t find_imp(uint32_t cls, const char *sel) {
    for (int depth = 0; cls && depth < 16; depth++) {
        if (cls < 0x100000 || cls >= 0x200000) return 0;            // external (fake) class: stop
        uint32_t ro = ro_of(cls);
        uint32_t ml = LD32(ro + 20);
        if (ml) {
            uint32_t n = LD32(ml + 4);
            for (uint32_t i = 0; i < n; i++) {
                uint32_t m = ml + 8 + i * 12;
                if (!strcmp(rt_cstr(LD32(m)), sel)) return LD32(m + 8);
            }
        }
        cls = LD32(cls + 4);
    }
    return 0;
}
static uint32_t image_class_by_instance(uint32_t obj) {
    if (obj < 0x20000000 || obj >= 0x30000000) return 0;
    uint32_t isa = LD32(obj);
    return is_image_class(isa) ? isa : 0;
}
// Instances of the app's own ObjC classes carry a 16-byte header in front of the object: [-16] block start,
// [-8] retain count, [-4] magic. retain/release/autorelease work like the real runtime and call the class's own dealloc.
#define IMG_MAGIC 0x1A0B1EC7u
static uint32_t image_alloc(uint32_t cls) {
    uint32_t size = LD32(ro_of(cls) + 8); if (size < 8) size = 8;
    uint32_t blk = rt_malloc(size + 16), o = blk + 16;
    memset(rt_ptr(blk), 0, size + 16);
    ST32(o - 8, 1); ST32(o - 4, IMG_MAGIC); ST32(o, cls);
    return o;
}


// ---- NSTimer / NSRunLoop (fired from pr3_frame) -------------------------------------------------------
// A scheduled timer is owned by the run loop (one retain) until it fires (non-repeating) or is invalidated.
typedef struct { uint32_t handle; double interval_ms; double remaining; uint32_t target, sel, user; int repeats, valid, scheduled, runloop_ref; } Timer;
#define MAX_TIMERS 4096
static Timer timers[MAX_TIMERS]; static int ntimers;
static Timer *timer_alloc(void) {
    for (int i = 0; i < ntimers; i++) if (!timers[i].handle) return &timers[i];
    return ntimers < MAX_TIMERS ? &timers[ntimers++] : NULL;
}
static Timer *timer_for(uint32_t h) { if (!h) return NULL; for (int i = 0; i < ntimers; i++) if (timers[i].handle == h) return &timers[i]; return NULL; }
static void timer_forget(uint32_t h) { Timer *t = timer_for(h); if (t) memset(t, 0, sizeof *t); }
static void timer_stop(Timer *t) {
    t->valid = 0;
    if (t->runloop_ref) { t->runloop_ref = 0; rc_release(t->handle); }     // may free the handle (and this entry)
}
void hle__objc_msgSend(CPU *c);
void rt_objc_fire_timers(double dt_ms) {
    uint32_t due[64]; int ndue = 0;
    for (int i = 0; i < ntimers; i++) {
        Timer *t = &timers[i];
        if (!t->valid || !t->scheduled) continue;
        t->remaining -= dt_ms;
        if (t->remaining <= 0 && ndue < 64) due[ndue++] = t->handle;
    }
    for (int k = 0; k < ndue; k++) {
        Timer *t = timer_for(due[k]); if (!t || !t->valid) continue;
        uint32_t h = t->handle; int repeats = t->repeats;
        if (getenv("PR3_TRACE_TIMERS")) fprintf(stderr, "[timer] fire %p\n", (void *)(uintptr_t)h);
        if (repeats) t->remaining = t->interval_ms;
        CPU c; memset(&c, 0, sizeof c);
        c.sp = RT_STACK_TOP - 0x2000; c.lr = 0x7ffff000u;
        c.r[0] = t->target; c.r[1] = t->sel; c.r[2] = h;
        hle__objc_msgSend(&c);
        if (!repeats) { t = timer_for(h); if (t) timer_stop(t); }
    }
}

static const char *sel_name(uint32_t s) { return rt_cstr(s); }

// ---- NSString -------------------------------------------------------------------------------------------
static const char *nsstr_c(uint32_t a, int *len) {
    Obj *o = obj_for(a);
    if (o && o->str) { if (len) *len = (int)strlen(o->str); return o->str; }
    if (a >= 0x100000 && a < 0x200000 && LD32(a + 12) < 100000) { if (len) *len = (int)LD32(a + 12); return rt_cstr(LD32(a + 8)); }   // CFString literal
    return NULL;
}
static uint32_t nsstr_new(const char *s);
uint32_t hle_objc_string_value(uint32_t a, char *out, int cap) { const char *s = nsstr_c(a, NULL); if (!s) s = ""; snprintf(out, cap, "%s", s); return (uint32_t)strlen(s); }
uint32_t hle_objc_new_string(const char *s) { Obj *o = obj_new("NSString", 0); o->str = strdup(s); return o->addr; }   // +1, owned by the caller (CFURL create rule)
static void pool_add(uint32_t a);
static uint32_t nsstr_new(const char *s) { Obj *o = obj_new("NSString", 0); o->str = strdup(s); pool_add(o->addr); return o->addr; }   // autoreleased
static void rc_retain(uint32_t a);
static uint32_t nsarray_new(void) { Obj *o = obj_new("NSArray", 0); pool_add(o->addr); return o->addr; }
static void nsarray_add(uint32_t arr, uint32_t item) { Obj *o = obj_for(arr); rc_retain(item); o->items = realloc(o->items, 4 * (o->nitems + 1)); o->items[o->nitems++] = item; }

static int exists(const char *path) { FILE *f = fopen(path, "rb"); if (f) { fclose(f); return 1; } return 0; }

// NSSearchPathForDirectoriesInDomains(directory, domainMask, expandTilde) -> NSArray of paths
void hle__NSSearchPathForDirectoriesInDomains(CPU *c) {
    uint32_t arr = nsarray_new(); nsarray_add(arr, nsstr_new(rt_documents_root)); c->r[0] = arr;
}

// Tests drive a virtual clock (one frame = 1/30 s) so runs are deterministic; the app uses the wall clock like the original.
double rt_virtual_now = -1;
static double now_seconds(void) { if (rt_virtual_now >= 0) return rt_virtual_now; struct timeval tv; gettimeofday(&tv, NULL); return (double)tv.tv_sec + tv.tv_usec / 1e6; }
static void ret_double(CPU *c, double d) { uint64_t u; memcpy(&u, &d, 8); c->r[0] = (uint32_t)u; c->r[1] = (uint32_t)(u >> 32); }
static void ret_float(CPU *c, float f) { c->r[0] = BITS_F32(f); }
static int nlog; static const char *logged[400]; static int nlogged;
static void log_unhandled(const char *cls, int is_class, const char *sel) {
    char key[160]; snprintf(key, sizeof key, "%c%s %s", is_class ? '+' : '-', cls, sel);
    for (int i = 0; i < nlogged; i++) if (!strcmp(logged[i], key)) return;
    if (nlogged < 400) logged[nlogged++] = strdup(key);
    if (nlog++ < 400) fprintf(stderr, "[objc] unhandled %s\n", key);
}

// ---- reference counting / autorelease pool ----
static uint32_t g_cur_sp;                          // guest stack pointer of the message being dispatched (for nested dealloc calls)
static int is_img_instance(uint32_t a) { return a >= RT_HEAP_BASE + 32 && a < HANDLE_BASE && LD32(a - 4) == IMG_MAGIC && image_class_by_instance(a); }
static void pool_add(uint32_t a) {
    if (npool == cappool) { cappool = cappool ? cappool * 2 : 1024; pool = realloc(pool, cappool * sizeof *pool); }
    pool[npool++] = a;
}
static uint32_t pool_cstr(const char *str) {
    uint32_t n = (uint32_t)strlen(str) + 1, p = rt_malloc(n); memcpy(rt_ptr(p), str, n);
    if (npool_cs == cap_cs) { cap_cs = cap_cs ? cap_cs * 2 : 256; pool_cs = realloc(pool_cs, cap_cs * sizeof *pool_cs); }
    pool_cs[npool_cs++] = p; return p;
}
static void rc_retain(uint32_t a) {
    Obj *o = obj_for(a);
    if (o) { if (o->rc < IMMORTAL) o->rc++; return; }
    if (is_img_instance(a)) ST32(a - 8, LD32(a - 8) + 1);
}
static void img_dealloc(uint32_t a) {
    uint32_t imp = find_imp(image_class_by_instance(a), "dealloc");
    if (imp) {
        CPU c; memset(&c, 0, sizeof c);
        c.sp = (g_cur_sp ? g_cur_sp - 0x200 : RT_STACK_TOP - 0x4000) & ~7u; c.lr = 0x7ffff000u;
        c.r[0] = a; c.r[1] = rt_new_cstr("dealloc");
        ST32(a - 8, 0x40000000u);                  // [super dealloc] and re-entrant releases must not free it again
        rt_call_indirect(&c, imp);
    }
    ST32(a - 4, 0);
    rt_free(a - 16);
}
static void rc_release(uint32_t a) {
    Obj *o = obj_for(a);
    if (o) { if (o->rc >= IMMORTAL) return; if (--o->rc <= 0) obj_free(o); return; }
    if (is_img_instance(a)) {
        int rc = (int)LD32(a - 8);
        if (rc >= 0x40000000) return;
        ST32(a - 8, (uint32_t)(rc - 1));
        if (rc - 1 == 0) img_dealloc(a);
    }
}
// Called once per frame: the original drained its autorelease pool at the end of every run-loop iteration.
void rt_objc_drain_pool(void) {
    while (npool) { uint32_t a = pool[--npool]; rc_release(a); }
    for (int i = 0; i < npool_cs; i++) rt_free(pool_cs[i]);
    npool_cs = 0;
}

static int dispatch_image(CPU *c, uint32_t self, const char *sel) {
    uint32_t klass = 0, imp;
    if (is_image_class(self)) {
        if (!strcmp(sel, "alloc") || !strcmp(sel, "allocWithZone:")) { c->r[0] = image_alloc(self); return 1; }
        klass = LD32(self);                    // metaclass
        imp = find_imp(klass, sel);
    } else {
        klass = image_class_by_instance(self);
        if (!klass) return 0;
        imp = find_imp(klass, sel);
        if (!imp) {
            if (!strcmp(sel, "init")) return 1;
            if (!strcmp(sel, "retain")) { rc_retain(self); return 1; }
            if (!strcmp(sel, "autorelease")) { if (is_img_instance(self)) pool_add(self); return 1; }
            if (!strcmp(sel, "release")) { rc_release(self); c->r[0] = 0; return 1; }
            if (!strcmp(sel, "dealloc")) { c->r[0] = 0; return 1; }
            if (!strcmp(sel, "respondsToSelector:")) { c->r[0] = find_imp(klass, rt_cstr(c->r[2])) != 0; return 1; }
            if (!strcmp(sel, "class")) { c->r[0] = klass; return 1; }
            if (!strcmp(sel, "isKindOfClass:") || !strcmp(sel, "isMemberOfClass:")) { c->r[0] = c->r[2] == klass; return 1; }
        }
    }
    if (!imp) return 0;
    rt_call_indirect(c, imp);
    return 1;
}

void hle__objc_msgSend_inner(CPU *c);
void hle__objc_msgSend(CPU *c) {
    uint32_t saved = g_cur_sp; g_cur_sp = c->sp;
    hle__objc_msgSend_inner(c);
    g_cur_sp = saved;
}
void hle__objc_msgSend_inner(CPU *c) {
    uint32_t self = c->r[0]; const char *sel = sel_name(c->r[1]);
    if (!self) { c->r[0] = 0; return; }
    if (dispatch_image(c, self, sel)) return;
    Obj *o = obj_for(self);
    const char *cls = o ? o->cls : "(raw)";
    int is_class = o && o->is_class;

    // universal
    if (!strcmp(sel, "alloc")) {
        if (is_class) { Obj *n = obj_new(cls, 0); c->r[0] = n->addr; return; }
        c->r[0] = 0; return;
    }
    if (!strcmp(sel, "init") || !strcmp(sel, "Init")) { return; }
    if (!strcmp(sel, "retain") || !strcmp(sel, "copy") || !strcmp(sel, "mutableCopy")) { rc_retain(self); return; }
    if (!strcmp(sel, "autorelease")) { if (o && o->rc < IMMORTAL) pool_add(self); return; }
    if (!strcmp(sel, "release")) { rc_release(self); c->r[0] = 0; return; }
    if (!strcmp(sel, "dealloc") || !strcmp(sel, "Kill")) { c->r[0] = 0; return; }
    if (!strcmp(sel, "respondsToSelector:")) { c->r[0] = 1; return; }
    if (!strcmp(sel, "isKindOfClass:")) { c->r[0] = 0; return; }

    // NSString
    if (!strcmp(sel, "UTF8String")) { const char *s = nsstr_c(self, NULL); c->r[0] = s ? pool_cstr(s) : 0; return; }
    if (!strcmp(sel, "stringWithUTF8String:") || !strcmp(sel, "stringWithCString:encoding:") || !strcmp(sel, "stringWithString:")) {
        const char *s = !strcmp(sel, "stringWithString:") ? nsstr_c(c->r[2], NULL) : rt_cstr(c->r[2]); c->r[0] = nsstr_new(s ? s : ""); return;
    }
    if (!strcmp(sel, "isEqualToString:")) { const char *a = nsstr_c(self, NULL), *b = nsstr_c(c->r[2], NULL); c->r[0] = (a && b && !strcmp(a, b)); return; }
    if (!strcmp(sel, "isAbsolutePath")) { const char *s = nsstr_c(self, NULL); c->r[0] = s && s[0] == '/'; return; }
    if (!strcmp(sel, "stringByAppendingString:")) {
        const char *a = nsstr_c(self, NULL), *b = nsstr_c(c->r[2], NULL); char buf[2048]; snprintf(buf, sizeof buf, "%s%s", a ? a : "", b ? b : ""); c->r[0] = nsstr_new(buf); return;
    }
    if (!strcmp(sel, "length")) { int n = 0; if (nsstr_c(self, &n)) c->r[0] = (uint32_t)n; else if (o) c->r[0] = (uint32_t)o->nitems; return; }

    // NSArray
    if (!strcmp(sel, "objectAtIndex:")) { c->r[0] = (o && c->r[2] < (uint32_t)o->nitems) ? o->items[c->r[2]] : 0; return; }
    if (!strcmp(sel, "count")) { c->r[0] = o ? (uint32_t)o->nitems : 0; return; }

    // NSBundle
    if (!strcmp(cls, "NSBundle")) {
        if (!strcmp(sel, "mainBundle")) { static uint32_t b; if (!b) b = hle_objc_new_object("NSBundle"); c->r[0] = b; return; }
        if (!strcmp(sel, "resourcePath")) { c->r[0] = nsstr_new(rt_resource_root); return; }
        if (!strcmp(sel, "pathForResource:ofType:")) {
            const char *name = nsstr_c(c->r[2], NULL), *type = c->r[3] ? nsstr_c(c->r[3], NULL) : NULL;
            char path[2048];
            if (type && *type) snprintf(path, sizeof path, "%s/%s.%s", rt_resource_root, name, type); else snprintf(path, sizeof path, "%s/%s", rt_resource_root, name);
            c->r[0] = exists(path) ? nsstr_new(path) : 0; return;
        }
        if (!strcmp(sel, "infoDictionary")) { c->r[0] = 0; return; }
    }
    // UIScreen / UIDevice
    if (!strcmp(cls, "UIScreen")) {
        if (!strcmp(sel, "mainScreen")) { static uint32_t s; if (!s) s = hle_objc_new_object("UIScreen"); c->r[0] = s; return; }
        if (!strcmp(sel, "scale")) { ret_float(c, 2.0f); return; }
    }
    if (!strcmp(cls, "UIDevice")) {
        if (!strcmp(sel, "currentDevice")) { static uint32_t s; if (!s) s = hle_objc_new_object("UIDevice"); c->r[0] = s; return; }
        if (!strcmp(sel, "systemVersion")) { c->r[0] = nsstr_new("4.3"); return; }
        if (!strcmp(sel, "model") || !strcmp(sel, "localizedModel")) { c->r[0] = nsstr_new("iPhone"); return; }
        if (!strcmp(sel, "systemName")) { c->r[0] = nsstr_new("iPhone OS"); return; }
        if (!strcmp(sel, "uniqueIdentifier")) { c->r[0] = nsstr_new("pr3-remake-device"); return; }
    }
    // NSDate (the race timer measures elapsed wall-clock time with it)
    if (!strcmp(cls, "NSDate")) {
        if (is_class && !strcmp(sel, "date")) { Obj *d = obj_new("NSDate", 0); d->t = now_seconds(); pool_add(d->addr); c->r[0] = d->addr; return; }
        if (is_class && !strcmp(sel, "dateWithTimeIntervalSince1970:")) {
            uint64_t u = ((uint64_t)c->r[3] << 32) | c->r[2]; double v; memcpy(&v, &u, 8);
            Obj *d = obj_new("NSDate", 0); d->t = v; pool_add(d->addr); c->r[0] = d->addr; return;
        }
        if (!is_class && !strcmp(sel, "timeIntervalSinceDate:")) { Obj *other = obj_for(c->r[2]); ret_double(c, o->t - (other ? other->t : 0)); return; }
        if (!is_class && !strcmp(sel, "timeIntervalSinceNow")) { ret_double(c, o->t - now_seconds()); return; }
        if (!is_class && !strcmp(sel, "timeIntervalSince1970")) { ret_double(c, o->t); return; }
    }
    // NSTimer / NSRunLoop / NSThread
    if (!strcmp(cls, "NSTimer")) {
        if (!strcmp(sel, "timerWithTimeInterval:target:selector:userInfo:repeats:") || !strcmp(sel, "scheduledTimerWithTimeInterval:target:selector:userInfo:repeats:")) {
            uint64_t u = ((uint64_t)c->r[3] << 32) | c->r[2]; double secs; memcpy(&secs, &u, 8);
            Obj *t = obj_new("NSTimer", 0); pool_add(t->addr);
            Timer *tm = timer_alloc();
            if (tm) {
                tm->handle = t->addr; tm->interval_ms = secs * 1000.0; tm->remaining = tm->interval_ms;
                tm->target = LD32(c->sp); tm->sel = LD32(c->sp + 4); tm->user = LD32(c->sp + 8); tm->repeats = LD32(c->sp + 12) & 0xff;
                tm->valid = 1; tm->scheduled = !strncmp(sel, "scheduled", 9);
                if (tm->scheduled) { tm->runloop_ref = 1; rc_retain(t->addr); }
                if (getenv("PR3_TRACE_TIMERS")) fprintf(stderr, "[timer] add %p interval %.1fms repeats %d target 0x%x sel %s\n", (void *)(uintptr_t)tm->handle, tm->interval_ms, tm->repeats, tm->target, rt_cstr(tm->sel));
            }
            c->r[0] = t->addr; return;
        }
    }
    if (o && !strcmp(o->cls, "NSTimer") && !o->is_class) {
        Timer *tm = timer_for(self);
        if (!strcmp(sel, "userInfo")) { c->r[0] = tm ? tm->user : 0; return; }
        if (!strcmp(sel, "isValid")) { c->r[0] = tm ? tm->valid : 0; return; }
        if (!strcmp(sel, "invalidate")) { if (tm) timer_stop(tm); c->r[0] = 0; return; }
    }
    if (!strcmp(cls, "NSRunLoop")) {
        if (!strcmp(sel, "mainRunLoop") || !strcmp(sel, "currentRunLoop")) { static uint32_t r; if (!r) r = hle_objc_new_object("NSRunLoop"); c->r[0] = r; return; }
        if (!strcmp(sel, "addTimer:forMode:")) { Timer *tm = timer_for(c->r[2]); if (tm) { tm->scheduled = 1; tm->remaining = tm->interval_ms; if (!tm->runloop_ref) { tm->runloop_ref = 1; rc_retain(tm->handle); } } c->r[0] = 0; return; }
    }
    if (!strcmp(cls, "NSThread") && !strcmp(sel, "isMainThread")) { c->r[0] = 1; return; }
    if (!strcmp(sel, "performSelector:withObject:")) { c->r[1] = c->r[2]; c->r[2] = c->r[3]; hle__objc_msgSend(c); return; }
    // the render view the engine drives
    if (!strcmp(sel, "startUpdateView")) { if (rt_shell_start_update) rt_shell_start_update(); return; }
    if (!strcmp(sel, "endUpdateView")) { if (rt_shell_end_update) rt_shell_end_update(); return; }

    if (rt_shell_hook) { rt_shell_hook(sel, self, c); }
    log_unhandled(cls, is_class, sel);
    c->r[0] = 0;
}
void hle__objc_msgSend_stret(CPU *c) { log_unhandled("(stret)", 0, sel_name(c->r[2])); c->r[0] = 0; }
// objc_msgSendSuper2(struct { id receiver; Class current_class; } *super, SEL, ...): look up in the superclass of current_class
void hle__objc_msgSendSuper2(CPU *c) {
    uint32_t sup = c->r[0]; uint32_t receiver = LD32(sup), cur = LD32(sup + 4);
    const char *sel = sel_name(c->r[1]);
    uint32_t sc = cur ? LD32(cur + 4) : 0;       // superclass of the class the calling method belongs to
    uint32_t imp = (sc && sc >= 0x100000 && sc < 0x200000) ? find_imp(sc, sel) : 0;
    c->r[0] = receiver;
    if (imp) { rt_call_indirect(c, imp); return; }
    if (!strcmp(sel, "init")) return;
    if (!strcmp(sel, "retain")) { rc_retain(receiver); return; }
    if (!strcmp(sel, "autorelease")) { if (is_img_instance(receiver)) pool_add(receiver); return; }
    if (!strcmp(sel, "release")) { rc_release(receiver); c->r[0] = 0; return; }
    c->r[0] = 0;                                  // [super dealloc] et al.: the object is freed by rc_release
}
// objc_getProperty(self, _cmd, offset, atomic) / objc_setProperty(self, _cmd, offset, newValue, atomic, shouldCopy): retain the new value, release the old one
void hle__objc_getProperty(CPU *c) { uint32_t v = LD32(c->r[0] + c->r[2]); if (v) { rc_retain(v); Obj *o = obj_for(v); if (o ? o->rc < IMMORTAL : is_img_instance(v)) pool_add(v); } c->r[0] = v; }
void hle__objc_setProperty(CPU *c) { uint32_t slot = c->r[0] + c->r[2], old = LD32(slot), nv = c->r[3]; if (nv != old) { if (nv) rc_retain(nv); ST32(slot, nv); if (old) rc_release(old); } }
void hle__objc_sync_enter(CPU *c) { c->r[0] = 0; }
void hle__objc_sync_exit(CPU *c) { c->r[0] = 0; }

// Diagnostics for soak tests
void rt_objc_stats(void) {
    int live = 0, timers_valid = 0;
    for (int i = 0; i < nslots; i++) if (objs[i].live) live++;
    for (int i = 0; i < ntimers; i++) if (timers[i].handle && timers[i].valid) timers_valid++;
    fprintf(stderr, "[objc] live handles %d (slots %d), statics %d, pool %d, timers valid %d / used %d\n", live, nslots, nstatics, npool, timers_valid, ntimers);
}

void hle_objc_release(uint32_t a) { rc_release(a); }       // CFRelease() of objects created with the CF create rule
