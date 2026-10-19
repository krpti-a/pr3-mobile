// HLE of libc / libm / libgcc imports used by the recompiled code.
#include "arm_rt.h"
static inline float A(CPU *c, int i) { return F32_BITS(c->r[i]); }
#define RET_F(x) do { c->r[0] = BITS_F32((float)(x)); } while (0)

void hle__floorf(CPU *c) { RET_F(floorf(A(c, 0))); }
void hle__ceilf(CPU *c) { RET_F(ceilf(A(c, 0))); }
void hle__sqrtf(CPU *c) { RET_F(sqrtf(A(c, 0))); }
void hle__sinf(CPU *c) { RET_F(sin((double)A(c, 0))); }
void hle__cosf(CPU *c) { RET_F(cos((double)A(c, 0))); }
void hle__tanf(CPU *c) { RET_F(tan((double)A(c, 0))); }
void hle__atan2f(CPU *c) { RET_F(atan2((double)A(c, 0), (double)A(c, 1))); }
void hle__atanf(CPU *c) { RET_F(atan((double)A(c, 0))); }
void hle__fabsf(CPU *c) { RET_F(fabsf(A(c, 0))); }
void hle__powf(CPU *c) { RET_F(pow((double)A(c, 0), (double)A(c, 1))); }
void hle__fmodf(CPU *c) { RET_F(fmod((double)A(c, 0), (double)A(c, 1))); }
void hle__roundf(CPU *c) { RET_F(floor((double)A(c, 0) + 0.5)); }
void hle__memcpy(CPU *c) { memmove(rt_ptr(c->r[0]), rt_ptr(c->r[1]), c->r[2]); }
void hle__memmove(CPU *c) { memmove(rt_ptr(c->r[0]), rt_ptr(c->r[1]), c->r[2]); }
void hle__memset(CPU *c) { memset(rt_ptr(c->r[0]), (int)c->r[1], c->r[2]); }
static uint32_t zalloc(uint32_t n) { uint32_t p = rt_malloc(n); uint32_t rs = rt_usable_size(p); memset(rt_ptr(p), 0, rs < n ? n : rs); return p; }
// The original ran on a heap whose fresh pages were zero; some constructors rely on that, so new/malloc return zeroed memory.
void hle___Znwm(CPU *c) { c->r[0] = zalloc(c->r[0]); }
void hle___Znam(CPU *c) { c->r[0] = zalloc(c->r[0]); }
void hle__malloc(CPU *c) { c->r[0] = zalloc(c->r[0]); }
uint32_t rt_free_caller;
void hle___ZdlPv(CPU *c) { rt_free_caller = c->lr; rt_free(c->r[0]); }
void hle___ZdaPv(CPU *c) { rt_free_caller = c->lr; rt_free(c->r[0]); }
void hle__free(CPU *c) { rt_free_caller = c->lr; rt_free(c->r[0]); }
void hle___Unwind_SjLj_Register(CPU *c) { c->r[0] = 0; }
void hle___Unwind_SjLj_Unregister(CPU *c) { c->r[0] = 0; }
void hle__strlen(CPU *c) { c->r[0] = (uint32_t)strlen((char *)rt_ptr(c->r[0])); }
