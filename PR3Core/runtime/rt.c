#include "arm_rt.h"
#include <stdarg.h>

#include <execinfo.h>
uint32_t rt_watch_lo = 0xffffffffu, rt_watch_len = 0;
void rt_watch_hit(uint32_t a, uint32_t v, int size) {
    fprintf(stderr, "[watch] write%d 0x%x = 0x%x\n", size, a, v);
    void *bt[24]; int n = backtrace(bt, 24); backtrace_symbols_fd(bt, n, 2);
}
uint8_t *rt_img, *rt_heap, *rt_stack;
uint8_t rt_scratch[16];
uint32_t rt_fault_count;
static uint32_t heap_ptr = RT_HEAP_BASE;

void rt_fault(uint32_t addr, int write) {
    if (rt_fault_count++ < 20) fprintf(stderr, "[rt] bad guest address 0x%x\n", addr);
}

void rt_unimpl(const char *what) { fprintf(stderr, "[rt] UNIMPLEMENTED instruction: %s\n", what); abort(); }

void rt_missing(uint32_t addr) {
    static int n;
    if (n++ < 50) fprintf(stderr, "[rt] call to non-recompiled function 0x%x\n", addr);
}

void rt_unhandled_import(const char *name, CPU *c) {
    static const char *seen[512]; static int nseen;
    for (int i = 0; i < nseen; i++) if (seen[i] == name || !strcmp(seen[i], name)) { c->r[0] = 0; return; }
    if (nseen < 512) seen[nseen++] = name;
    fprintf(stderr, "[rt] unhandled import %s\n", name);
    c->r[0] = 0;
}

const char *rt_cstr(uint32_t a) { return (const char *)rt_ptr(a); }
uint32_t rt_new_cstr(const char *s) { uint32_t n = (uint32_t)strlen(s) + 1; uint32_t p = rt_alloc(n); memcpy(rt_ptr(p), s, n); return p; }

void hle_objc_bind(uint32_t slot, const char *sym);     // hle_objc.c
int rt_init(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(len);
    if (fread(buf, 1, len, f) != (size_t)len) { fclose(f); free(buf); return -2; }
    fclose(f);
    if (len < 1167360 + 57344) { free(buf); return -3; }
    // The shipped copy of the code image carries a scrambled Mach-O magic (tools/build_game.sh) so that Xcode does not
    // mistake it for an executable to strip and sign; the thin armv7 binary itself is accepted too.
    if (buf[0] == 0x31 && buf[1] == 0x05 && buf[2] == 0x12 && buf[3] == 0x01) { buf[0] = 0xCE; buf[1] = 0xFA; buf[2] = 0xED; buf[3] = 0xFE; }
    rt_img = calloc(1, RT_IMG_SIZE);
    // __TEXT: vm 0x1000, file [0, 1167360) ; __DATA: vm 0x11e000, file [1167360, +57344)
    memcpy(rt_img + 0x1000, buf, 1167360);
    memcpy(rt_img + 0x11e000, buf + 1167360, 57344);
    free(buf);
    rt_heap = calloc(1, RT_HEAP_SIZE);
    rt_stack = calloc(1, RT_STACK_SIZE);
    heap_ptr = RT_HEAP_BASE;
    for (uint32_t i = 0; i < rt_bind_count; i++) hle_objc_bind(rt_binds[i].addr, rt_binds[i].name);
    return 0;
}

// Run the C++ static initialisers (__DATA,__mod_init_func) of the original binary.
void rt_run_initializers(void) {
    for (uint32_t a = 0x11e870; a < 0x11e8d0; a += 4) {
        uint32_t fn = LD32(a);
        if (fn) rt_call(fn, NULL, 0, NULL);
    }
}

uint32_t rt_alloc(uint32_t size) {
    heap_ptr = (heap_ptr + 15) & ~15u;
    uint32_t p = heap_ptr;
    heap_ptr += size;
    if (heap_ptr > RT_HEAP_BASE + RT_HEAP_SIZE) { fprintf(stderr, "[rt] guest heap exhausted\n"); abort(); }
    return p;
}

// malloc/free on top of the bump allocator: 16-byte header (size class), size-class free lists.
// Classes: multiples of 16 up to 1 KB, then powers of two.
#define NCLASS 64
#define ALLOC_MAGIC 0xA110CA7Eu
static uint32_t freelist[NCLASS];
static uint32_t class_of(uint32_t size, uint32_t *rounded) {
    if (size <= 1024) { uint32_t rs = (size + 15) & ~15u; if (!rs) rs = 16; *rounded = rs; return rs / 16 - 1; }      // 0..63
    uint32_t rs = 2048, cls = 64;
    while (rs < size) { rs <<= 1; cls++; }
    *rounded = rs; return cls;                                                                                  // 64..
}
static uint32_t freelist_big[32];
uint32_t rt_malloc(uint32_t size) {
    uint32_t rs; uint32_t cls = class_of(size, &rs);
    uint32_t *head = cls < NCLASS ? &freelist[cls] : &freelist_big[cls - NCLASS];
    if (*head) { uint32_t p = *head; *head = LD32(p - 16); ST32(p - 12, ALLOC_MAGIC); return p; }
    uint32_t base = rt_alloc(rs + 16);
    uint32_t p = base + 16;
    ST32(p - 4, rs); ST32(p - 8, cls); ST32(p - 12, ALLOC_MAGIC);
    return p;
}
static int bad_free_logged; extern uint32_t rt_free_caller;
void rt_free(uint32_t p) {
    if (!p) return;
    if (p < RT_HEAP_BASE + 16 || p >= RT_HEAP_BASE + RT_HEAP_SIZE) {
        if (bad_free_logged++ < 40) fprintf(stderr, "[rt] free of non-heap pointer 0x%x ignored (caller 0x%x)\n", p, rt_free_caller);
        return;
    }
    uint32_t cls = LD32(p - 8);
    if (cls >= NCLASS + 32 || LD32(p - 12) != ALLOC_MAGIC) {
        if (bad_free_logged++ < 40) fprintf(stderr, "[rt] invalid/double free of 0x%x ignored (caller 0x%x)\n", p, rt_free_caller);
        return;
    }
    ST32(p - 12, 0xFEEDF4EE);
    uint32_t *head = cls < NCLASS ? &freelist[cls] : &freelist_big[cls - NCLASS];
    ST32(p - 16, *head); *head = p;      // free-list link lives in the header so stale payload (e.g. vtable pointers) survives like in the original malloc
}
uint32_t rt_usable_size(uint32_t p) { return p ? LD32(p - 4) : 0; }

int rt_hle_function(CPU *c, uint32_t addr);
void rt_call_indirect(CPU *c, uint32_t addr) {
    addr &= ~1u;
    if (addr >= 0x0ff00000u && addr < 0x0ff01000u && rt_hle_function(c, addr)) return;
    uint32_t lo = 0, hi = rt_table_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        // table is sorted by address within two runs (functions, then stubs) -> do linear fallback below
        if (rt_table[mid].addr < addr) lo = mid + 1; else hi = mid;
    }
    if (lo < rt_table_count && rt_table[lo].addr == addr) { rt_table[lo].fn(c); return; }
    for (uint32_t i = 0; i < rt_table_count; i++) if (rt_table[i].addr == addr) { rt_table[i].fn(c); return; }
    { static int n; if (n++ < 30) fprintf(stderr, "[rt] indirect call to unknown 0x%x (caller lr 0x%x) r0=0x%x [%x %x %x %x]\n", addr, c->lr, c->r[0], LD32(c->r[0]), LD32(c->r[0] + 4), LD32(c->r[0] + 8), LD32(c->r[0] + 12)); }
}

#define SENTINEL 0x7ffff000u
void rt_call(uint32_t addr, uint32_t *args, int nargs, uint32_t *ret) {
    CPU c; memset(&c, 0, sizeof c);
    uint32_t sp = RT_STACK_TOP - 0x1000;
    int extra = nargs > 4 ? nargs - 4 : 0;
    sp -= 4 * extra; sp &= ~7u;
    for (int i = 0; i < extra; i++) ST32(sp + 4 * i, args[4 + i]);
    for (int i = 0; i < nargs && i < 4; i++) c.r[i] = args[i];
    c.sp = sp; c.lr = SENTINEL;
    rt_call_indirect(&c, addr);
    if (ret) { ret[0] = c.r[0]; ret[1] = c.r[1]; }
}

#ifdef RT_TRACE
// Debug tracing: PR3_TRACE="4d0c8,58058" prints entries; PR3_TRACE_FIELDS="64,7a5" also prints those words of *r0.
static uint32_t trace_set[64]; static int ntrace = -1; static uint32_t trace_fields[16]; static int nfields; static uint32_t trace_fields1[16]; static int nfields1;
// PR3_COVERAGE=file writes the address of every recompiled function that ran (one hex address per line) at exit.
static uint8_t cov[0x42000 / 8 + 1]; static const char *cov_path; static int cov_init;
static void cov_dump(void) { FILE *f = fopen(cov_path, "w"); if (!f) return; for (uint32_t i = 0; i < 0x42000; i++) if (cov[i >> 3] & (1u << (i & 7))) fprintf(f, "%x\n", i * 4); fclose(f); }
void rt_trace_enter(uint32_t addr, CPU *c) {
    if (!cov_init) { cov_init = 1; cov_path = getenv("PR3_COVERAGE"); if (cov_path) atexit(cov_dump); }
    if (cov_path && (addr >> 2) < 0x42000) cov[(addr >> 2) >> 3] |= 1u << ((addr >> 2) & 7);
    if (ntrace < 0) {
        ntrace = 0;
        const char *t = getenv("PR3_TRACE");
        if (t) { char *s = strdup(t); for (char *tok = strtok(s, ","); tok && ntrace < 64; tok = strtok(NULL, ",")) trace_set[ntrace++] = (uint32_t)strtoul(tok, 0, 16); }
        const char *f = getenv("PR3_TRACE_FIELDS");
        if (f) { char *s = strdup(f); for (char *tok = strtok(s, ","); tok && nfields < 16; tok = strtok(NULL, ",")) trace_fields[nfields++] = (uint32_t)strtoul(tok, 0, 16); }
        const char *f1 = getenv("PR3_TRACE_FIELDS1");
        if (f1) { char *s = strdup(f1); for (char *tok = strtok(s, ","); tok && nfields1 < 16; tok = strtok(NULL, ",")) trace_fields1[nfields1++] = (uint32_t)strtoul(tok, 0, 16); }
    }
    for (int i = 0; i < ntrace; i++) if (trace_set[i] == addr) {
        fprintf(stderr, "[trace] enter 0x%x r0=0x%x r1=0x%x r2=0x%x r3=0x%x lr=0x%x", addr, c->r[0], c->r[1], c->r[2], c->r[3], c->lr);
        for (int k = 0; k < nfields; k++) fprintf(stderr, " [+%x]=%x", trace_fields[k], LD32(c->r[0] + trace_fields[k]));
        for (int k = 0; k < nfields1; k++) fprintf(stderr, " r1[+%x]=%x", trace_fields1[k], LD32(c->r[1] + trace_fields1[k]));
        if (getenv("PR3_TRACE_VEC")) { uint32_t off = (uint32_t)strtoul(getenv("PR3_TRACE_VEC"), 0, 16); uint32_t b = LD32(c->r[0] + off), e = LD32(c->r[0] + off + 4); fprintf(stderr, " vec:"); for (uint32_t p = b; p < e && p < b + 64; p += 4) { uint32_t o = LD32(p); fprintf(stderr, " %x(vp=%x)", o, o ? LD32(o) : 0); } }
        if (getenv("PR3_TRACE_STR")) {       // print r1..r3 as std::string* / char* where they look like one
            for (int k = 1; k < 4; k++) { uint32_t a = c->r[k]; if (a > 0x20000000 && a < 0x70200000) { uint32_t d = LD32(a); const char *t = (d > 0x1000 && d < 0x70200000) ? rt_cstr(d) : ""; fprintf(stderr, " r%d=\"%.40s\"", k, t); } else if (a > 0x1000 && a < 0x200000) fprintf(stderr, " r%d=c\"%.40s\"", k, rt_cstr(a)); }
        }
        fprintf(stderr, " stack=[%x %x %x %x]", LD32(c->sp), LD32(c->sp + 4), LD32(c->sp + 8), LD32(c->sp + 12));
        fputc('\n', stderr);
        return;
    }
}
#endif
