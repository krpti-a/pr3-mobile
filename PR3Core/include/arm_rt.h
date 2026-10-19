// Runtime for the statically recompiled PR3 gameplay code (armv7 -> C).
#ifndef ARM_RT_H
#define ARM_RT_H
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct CPU {
    uint32_t r[4];     // r0-r3: arguments / return values
    uint32_t sp;
    uint32_t lr;       // return address as seen by the callee
} CPU;

// ---- guest memory --------------------------------------------------------
#define RT_IMG_SIZE   0x200000u        // original binary image, guest addresses 0 .. 0x1fffff
#define RT_HEAP_BASE  0x20000000u
#define RT_HEAP_SIZE  0x10000000u
#define RT_STACK_TOP  0x70100000u
#define RT_STACK_SIZE 0x00100000u

extern uint8_t *rt_img, *rt_heap, *rt_stack;
extern uint8_t rt_scratch[16];
extern uint32_t rt_fault_count;
void rt_fault(uint32_t addr, int write);

static inline uint8_t *rt_ptr(uint32_t a) {
    if (a < RT_IMG_SIZE) return rt_img + a;
    uint32_t h = a - RT_HEAP_BASE;
    if (h < RT_HEAP_SIZE) return rt_heap + h;
    uint32_t s = a - (RT_STACK_TOP - RT_STACK_SIZE);
    if (s < RT_STACK_SIZE) return rt_stack + s;
    rt_fault(a, 0);
    return rt_scratch;
}
static inline uint32_t LD32(uint32_t a) { uint32_t v; memcpy(&v, rt_ptr(a), 4); return v; }
static inline uint32_t LD16(uint32_t a) { uint16_t v; memcpy(&v, rt_ptr(a), 2); return v; }
static inline uint32_t LD8(uint32_t a) { return *rt_ptr(a); }
static inline uint32_t LDS8(uint32_t a) { return (uint32_t)(int32_t)(int8_t)*rt_ptr(a); }
static inline uint32_t LDS16(uint32_t a) { int16_t v; memcpy(&v, rt_ptr(a), 2); return (uint32_t)(int32_t)v; }
extern uint32_t rt_watch_lo, rt_watch_len;
void rt_watch_hit(uint32_t a, uint32_t v, int size);
static inline void ST32(uint32_t a, uint32_t v) { if (__builtin_expect(a - rt_watch_lo < rt_watch_len, 0)) rt_watch_hit(a, v, 4); memcpy(rt_ptr(a), &v, 4); }
static inline void ST16(uint32_t a, uint32_t v) { if (__builtin_expect(a - rt_watch_lo < rt_watch_len, 0)) rt_watch_hit(a, v, 2); uint16_t x = (uint16_t)v; memcpy(rt_ptr(a), &x, 2); }
static inline void ST8(uint32_t a, uint32_t v) { if (__builtin_expect(a - rt_watch_lo < rt_watch_len, 0)) rt_watch_hit(a, v, 1); *rt_ptr(a) = (uint8_t)v; }
static inline float F32_BITS(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline uint32_t BITS_F32(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline float LDF(uint32_t a) { float f; memcpy(&f, rt_ptr(a), 4); return f; }
static inline void STF(uint32_t a, float f) { memcpy(rt_ptr(a), &f, 4); }
static inline double D2F(float lo, float hi) { uint32_t l = BITS_F32(lo), h = BITS_F32(hi); uint64_t u = ((uint64_t)h << 32) | l; double d; memcpy(&d, &u, 8); return d; }
static inline void F2D(double d, float *lo, float *hi) { uint64_t u; memcpy(&u, &d, 8); *lo = F32_BITS((uint32_t)u); *hi = F32_BITS((uint32_t)(u >> 32)); }
static inline uint32_t CVT_S32(double d) { if (d != d) return 0; if (d >= 2147483647.0) return 0x7fffffffu; if (d <= -2147483648.0) return 0x80000000u; return (uint32_t)(int32_t)d; }
static inline uint32_t CVT_U32(double d) { if (d != d || d <= 0) return 0; if (d >= 4294967295.0) return 0xffffffffu; return (uint32_t)d; }

// ---- shifts -----------------------------------------------------------------
static inline uint32_t LSL(uint32_t x, uint32_t s) { return s >= 32 ? 0 : x << s; }
static inline uint32_t LSR(uint32_t x, uint32_t s) { return s >= 32 ? 0 : x >> s; }
static inline uint32_t ASR(uint32_t x, uint32_t s) { return (uint32_t)((int32_t)x >> (s >= 32 ? 31 : s)); }
static inline uint32_t ROR(uint32_t x, uint32_t s) { s &= 31; return s ? (x >> s) | (x << (32 - s)) : x; }

#define FCMP(x_, y_) do { if ((x_) != (x_) || (y_) != (y_)) { fn = 0; fz = 0; fc = 1; fv = 1; } \
    else if ((x_) == (y_)) { fn = 0; fz = 1; fc = 1; fv = 0; } \
    else if ((x_) < (y_)) { fn = 1; fz = 0; fc = 0; fv = 0; } \
    else { fn = 0; fz = 0; fc = 1; fv = 0; } } while (0)

// ---- calls --------------------------------------------------------------------
typedef void (*rt_fn)(CPU *);
void rt_call_indirect(CPU *c, uint32_t addr);
void rt_unimpl(const char *what);
void rt_missing(uint32_t addr);

#define UNIMPL(msg) rt_unimpl(msg)
#define CALL(F, NXT) do { c->r[0] = r0; c->r[1] = r1; c->r[2] = r2; c->r[3] = r3; c->sp = sp; c->lr = (NXT); \
    F(c); r0 = c->r[0]; r1 = c->r[1]; r2 = c->r[2]; r3 = c->r[3]; } while (0)
#define CALL_IND(A, NXT) do { c->r[0] = r0; c->r[1] = r1; c->r[2] = r2; c->r[3] = r3; c->sp = sp; c->lr = (NXT); \
    rt_call_indirect(c, (A)); r0 = c->r[0]; r1 = c->r[1]; r2 = c->r[2]; r3 = c->r[3]; } while (0)
#define TAILCALL(F) do { c->r[0] = r0; c->r[1] = r1; c->r[2] = r2; c->r[3] = r3; c->sp = sp; c->lr = entry_lr; F(c); return; } while (0)
#define RETURN_() do { c->r[0] = r0; c->r[1] = r1; c->r[2] = r2; c->r[3] = r3; return; } while (0)

#ifdef RT_TRACE
void rt_trace_enter(uint32_t addr, CPU *c);
#define RT_ENTER(A) rt_trace_enter((A), c)
#else
#define RT_ENTER(A) ((void)0)
#endif

typedef struct { uint32_t addr; const char *name; } rt_stub_name;
extern const rt_stub_name rt_stub_names[];
extern const uint32_t rt_stub_name_count;

// Every recompiled function table entry
typedef struct { uint32_t addr; rt_fn fn; } rt_entry;
extern const rt_entry rt_table[];
extern const uint32_t rt_table_count;

typedef struct { uint32_t addr; const char *name; int addend; } rt_bind;
extern const rt_bind rt_binds[];
extern const uint32_t rt_bind_count;

const char *rt_cstr(uint32_t addr);               // host pointer to a guest C string
uint32_t rt_new_cstr(const char *s);              // allocate a guest C string
// Public API ------------------------------------------------------------------
int  rt_init(const char *binary_path);          // loads the original image, sets up heap/stack
uint32_t rt_alloc(uint32_t size);      // bump allocator (never freed)
uint32_t rt_malloc(uint32_t size);     // malloc/free used by guest new/delete
void rt_free(uint32_t p);
uint32_t rt_usable_size(uint32_t p);
void rt_call(uint32_t addr, uint32_t *args, int nargs, uint32_t *ret);
uint32_t rt_sym(const char *name);
#endif
