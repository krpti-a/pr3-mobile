// Minimal runtime for the instruction-level differential test (see tools/verify/run.py).
#include "arm_rt.h"
#include <sys/mman.h>

uint8_t *rt_img, *rt_heap, *rt_stack;
uint8_t rt_scratch[16];
uint32_t rt_fault_count;
uint32_t rt_watch_lo = 0xffffffffu, rt_watch_len = 0;
void rt_fault(uint32_t a, int w) { rt_fault_count++; }
void rt_watch_hit(uint32_t a, uint32_t v, int size) {}
void rt_unimpl(const char *what) { fprintf(stderr, "UNIMPL %s\n", what); }
void rt_missing(uint32_t addr) {}
void rt_call_indirect(CPU *c, uint32_t addr) {}

typedef struct { uint32_t r[13], sp, lr, n, z, cf, v, fn, fz, fc, fv; float s[34]; } TS;
extern void (*const ts_funcs[])(void *);
extern const int ts_nfuncs;

int ts_init(const char *bin) {
    FILE *f = fopen(bin, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(n); if (fread(buf, 1, n, f) != (size_t)n) return -2; fclose(f);
    rt_img = calloc(1, RT_IMG_SIZE);
    memcpy(rt_img + 0x1000, buf, 1167360);
    memcpy(rt_img + 0x11e000, buf + 1167360, 57344);
    free(buf);
    rt_heap = mmap(NULL, RT_HEAP_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    rt_stack = calloc(1, RT_STACK_SIZE);
    return ts_nfuncs;
}
uint8_t *ts_window(void) { return rt_heap; }
uint32_t ts_faults(void) { return rt_fault_count; }
void ts_run(int i, TS *t) { rt_fault_count = 0; ts_funcs[i](t); }
