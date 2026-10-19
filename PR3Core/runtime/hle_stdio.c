// HLE of stdio / printf / string / time / misc libc imports for the recompiled game.
#include "arm_rt.h"
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>

char rt_resource_root[1024] = ".";      // directory holding the original game's data files
char rt_documents_root[1024] = "/tmp";  // writable directory (settings, saves)

// ---- guest variadic argument reader (AAPCS, softfp) --------------------------------------------
typedef struct { CPU *c; int reg; uint32_t stack; uint32_t valist; int use_valist; } ArgReader;
static uint32_t arg32(ArgReader *a) {
    if (a->use_valist) { uint32_t v = LD32(a->valist); a->valist += 4; return v; }
    if (a->reg < 4) return a->c->r[a->reg++];
    uint32_t v = LD32(a->stack); a->stack += 4; return v;
}
static uint64_t arg64(ArgReader *a) {
    if (a->use_valist) { a->valist = (a->valist + 7) & ~7u; uint64_t lo = LD32(a->valist), hi = LD32(a->valist + 4); a->valist += 8; return lo | (hi << 32); }
    if (a->reg < 4) {
        if (a->reg & 1) a->reg++;                         // 64-bit values start on an even register
        if (a->reg < 4) { uint64_t lo = a->c->r[a->reg], hi = a->c->r[a->reg + 1]; a->reg += 2; return lo | (hi << 32); }
    }
    a->stack = (a->stack + 7) & ~7u;
    uint64_t lo = LD32(a->stack), hi = LD32(a->stack + 4); a->stack += 8;
    return lo | (hi << 32);
}

// returns number of characters that would be written; writes at most n-1 to out (if out != NULL)
static int guest_format(char *out, size_t n, const char *fmt, ArgReader *ar) {
    size_t pos = 0;
    int total = 0;
    #define PUT(ch) do { if (out && pos + 1 < n) out[pos] = (ch); pos++; total++; } while (0)
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { PUT(*p); continue; }
        char spec[32]; int si = 0; spec[si++] = *p++;
        while (*p && strchr("-+ #0", *p) && si < 20) spec[si++] = *p++;
        if (*p == '*') { int w = (int)arg32(ar); si += snprintf(spec + si, 12, "%d", w); p++; }
        else while (*p && isdigit((unsigned char)*p) && si < 24) spec[si++] = *p++;
        if (*p == '.') {
            spec[si++] = *p++;
            if (*p == '*') { int w = (int)arg32(ar); si += snprintf(spec + si, 12, "%d", w); p++; }
            else while (*p && isdigit((unsigned char)*p) && si < 28) spec[si++] = *p++;
        }
        int lng = 0;
        while (*p && strchr("hlLqjzt", *p)) { if (*p == 'l' || *p == 'q' || *p == 'L') lng++; p++; }
        char conv = *p; if (!conv) break;
        char buf[512]; int len = 0;
        switch (conv) {
            case '%': PUT('%'); continue;
            case 'c': { spec[si++] = 'c'; spec[si] = 0; len = snprintf(buf, sizeof buf, spec, (int)arg32(ar)); break; }
            case 's': {
                uint32_t sp = arg32(ar); const char *s = sp ? rt_cstr(sp) : "(null)";
                spec[si++] = 's'; spec[si] = 0;
                len = (int)strlen(s);
                if (len > 400) { for (const char *q = s; *q; q++) PUT(*q); continue; }
                len = snprintf(buf, sizeof buf, spec, s); break;
            }
            case 'd': case 'i': {
                if (lng >= 2) { spec[si++] = 'l'; spec[si++] = 'l'; spec[si++] = 'd'; spec[si] = 0; len = snprintf(buf, sizeof buf, spec, (long long)arg64(ar)); }
                else { spec[si++] = 'd'; spec[si] = 0; len = snprintf(buf, sizeof buf, spec, (int)arg32(ar)); }
                break;
            }
            case 'u': case 'x': case 'X': case 'o': {
                if (lng >= 2) { spec[si++] = 'l'; spec[si++] = 'l'; spec[si++] = conv; spec[si] = 0; len = snprintf(buf, sizeof buf, spec, (unsigned long long)arg64(ar)); }
                else { spec[si++] = conv; spec[si] = 0; len = snprintf(buf, sizeof buf, spec, (unsigned)arg32(ar)); }
                break;
            }
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
                uint64_t u = arg64(ar); double d; memcpy(&d, &u, 8);
                spec[si++] = conv; spec[si] = 0; len = snprintf(buf, sizeof buf, spec, d); break;
            }
            case 'p': { spec[si++] = 'x'; spec[si] = 0; len = snprintf(buf, sizeof buf, "0x%x", (unsigned)arg32(ar)); break; }
            default: PUT('%'); PUT(conv); continue;
        }
        if (len >= (int)sizeof buf) len = sizeof buf - 1;
        for (int i = 0; i < len; i++) PUT(buf[i]);
    }
    if (out && n) out[pos < n ? pos : n - 1] = 0;
    return total;
    #undef PUT
}

static void do_fmt_regs(CPU *c, int fmt_reg, char *out, size_t n, int *ret) {
    ArgReader ar = { c, fmt_reg + 1, c->sp, 0, 0 };
    int r = guest_format(out, n, rt_cstr(c->r[fmt_reg]), &ar);
    if (ret) *ret = r;
}

void hle__sprintf(CPU *c) { char *o = (char *)rt_ptr(c->r[0]); int r; do_fmt_regs(c, 1, o, 0x7fffffff, &r); c->r[0] = r; }
void hle____sprintf_chk(CPU *c) {            // (dst, flag, dstlen, fmt, ...)
    char *o = (char *)rt_ptr(c->r[0]); ArgReader ar = { c, 4, c->sp, 0, 0 };
    c->r[0] = guest_format(o, c->r[2], rt_cstr(c->r[3]), &ar);
}
void hle__snprintf(CPU *c) { char *o = (char *)rt_ptr(c->r[0]); int r; ArgReader ar = { c, 3, c->sp, 0, 0 }; r = guest_format(o, c->r[1], rt_cstr(c->r[2]), &ar); c->r[0] = r; }
void hle__vsnprintf(CPU *c) { ArgReader ar = { c, 4, 0, c->r[3], 1 }; c->r[0] = guest_format((char *)rt_ptr(c->r[0]), c->r[1], rt_cstr(c->r[2]), &ar); }
void hle__vsprintf(CPU *c) { ArgReader ar = { c, 3, 0, c->r[2], 1 }; c->r[0] = guest_format((char *)rt_ptr(c->r[0]), 0x7fffffff, rt_cstr(c->r[1]), &ar); }
void hle__printf(CPU *c) { char buf[1024]; do_fmt_regs(c, 0, buf, sizeof buf, NULL); fputs(buf, stderr); c->r[0] = 0; }
void hle__puts(CPU *c) { fprintf(stderr, "%s\n", rt_cstr(c->r[0])); c->r[0] = 0; }
void hle__putchar(CPU *c) { fputc((int)c->r[0], stderr); }
void hle__NSLog(CPU *c) { (void)c; }

// ---- stdio ----------------------------------------------------------------------------------------
#define MAXF 64
static FILE *files[MAXF]; static uint32_t file_guest[MAXF];
static int file_slot(uint32_t g) { for (int i = 0; i < MAXF; i++) if (file_guest[i] == g && files[i]) return i; return -1; }
char rt_last_fopen[256];
void hle__fopen(CPU *c) {
    const char *path = rt_cstr(c->r[0]), *mode = rt_cstr(c->r[1]);
    snprintf(rt_last_fopen, sizeof rt_last_fopen, "%s", path);
    char full[2048];
    if (path[0] == '/') snprintf(full, sizeof full, "%s", path);
    else snprintf(full, sizeof full, "%s/%s", rt_resource_root, path);
    FILE *f = fopen(full, mode);
    if (!f && path[0] == '/' ) { // allow bundle paths to resolve into the resource root by basename
        const char *b = strrchr(path, '/'); snprintf(full, sizeof full, "%s/%s", rt_resource_root, b + 1); f = fopen(full, mode);
    }
    if (!f) { c->r[0] = 0; return; }
    for (int i = 0; i < MAXF; i++) if (!files[i]) { files[i] = f; file_guest[i] = rt_alloc(16); c->r[0] = file_guest[i]; return; }
    fclose(f); c->r[0] = 0;
}
void hle__fclose(CPU *c) { int s = file_slot(c->r[0]); if (s >= 0) { fclose(files[s]); files[s] = NULL; } c->r[0] = 0; }
void hle__fread(CPU *c) { int s = file_slot(c->r[3]); c->r[0] = s < 0 ? 0 : (uint32_t)fread(rt_ptr(c->r[0]), c->r[1], c->r[2], files[s]); }
void hle__fwrite(CPU *c) { int s = file_slot(c->r[3]); c->r[0] = s < 0 ? 0 : (uint32_t)fwrite(rt_ptr(c->r[0]), c->r[1], c->r[2], files[s]); }
void hle__fseek(CPU *c) { int s = file_slot(c->r[0]); c->r[0] = s < 0 ? (uint32_t)-1 : (uint32_t)fseek(files[s], (long)(int32_t)c->r[1], (int)c->r[2]); }
void hle__ftell(CPU *c) { int s = file_slot(c->r[0]); c->r[0] = s < 0 ? (uint32_t)-1 : (uint32_t)ftell(files[s]); }

// ---- strings -----------------------------------------------------------------------------------------
void hle__strcpy(CPU *c) { strcpy((char *)rt_ptr(c->r[0]), rt_cstr(c->r[1])); }
void hle__strncpy(CPU *c) { strncpy((char *)rt_ptr(c->r[0]), rt_cstr(c->r[1]), c->r[2]); }
void hle__strncmp(CPU *c) { c->r[0] = (uint32_t)strncmp(rt_cstr(c->r[0]), rt_cstr(c->r[1]), c->r[2]); }
void hle__strcasecmp(CPU *c) { c->r[0] = (uint32_t)strcasecmp(rt_cstr(c->r[0]), rt_cstr(c->r[1])); }
void hle__strncasecmp(CPU *c) { c->r[0] = (uint32_t)strncasecmp(rt_cstr(c->r[0]), rt_cstr(c->r[1]), c->r[2]); }
void hle__strdup(CPU *c) { const char *src = rt_cstr(c->r[0]); uint32_t n = (uint32_t)strlen(src) + 1, p = rt_malloc(n); memcpy(rt_ptr(p), src, n); c->r[0] = p; }   // guest code free()s it
void hle__strerror(CPU *c) { c->r[0] = rt_new_cstr("error"); }
void hle__strstr(CPU *c) {
    const char *h = rt_cstr(c->r[0]); const char *f = strstr(h, rt_cstr(c->r[1]));
    c->r[0] = f ? c->r[0] + (uint32_t)(f - h) : 0;
}
void hle__atoi(CPU *c) { c->r[0] = (uint32_t)atoi(rt_cstr(c->r[0])); }
void hle__atof(CPU *c) { double d = atof(rt_cstr(c->r[0])); uint64_t u; memcpy(&u, &d, 8); c->r[0] = (uint32_t)u; c->r[1] = (uint32_t)(u >> 32); }
void hle___ToUpper(CPU *c) { c->r[0] = (uint32_t)toupper((int)c->r[0]); }
void hle____toupper(CPU *c) { c->r[0] = (uint32_t)toupper((int)c->r[0]); }
// BSD _RuneLocale (32-bit): magic[8] encoding[32] sgetrune sputrune invalid_rune | __runetype[256] __maplower[256] __mapupper[256] ...
// The original's inline ctype macros index __runetype directly, so it must exist in guest memory.
static uint32_t rune_type(int ch) {
    uint32_t f = 0;
    if (ch < 0 || ch > 127) return 0;
    if (isalpha(ch)) f |= 0x100;
    if (iscntrl(ch)) f |= 0x200;
    if (isdigit(ch)) f |= 0x400 | (uint32_t)(ch - '0');
    if (isgraph(ch)) f |= 0x800;
    if (islower(ch)) f |= 0x1000;
    if (ispunct(ch)) f |= 0x2000;
    if (isspace(ch)) f |= 0x4000;
    if (isupper(ch)) f |= 0x8000;
    if (isxdigit(ch)) { f |= 0x10000; if (!isdigit(ch)) f |= (uint32_t)(tolower(ch) - 'a' + 10); }
    if (ch == ' ' || ch == '\t') f |= 0x20000;
    if (isprint(ch)) f |= 0x40000;
    return f;
}
uint32_t rt_make_rune_locale(void) {
    uint32_t p = rt_alloc(4096);
    memcpy(rt_ptr(p), "RuneMagA", 8);
    for (int i = 0; i < 256; i++) {
        ST32(p + 52 + 4 * i, rune_type(i));
        ST32(p + 52 + 1024 + 4 * i, (uint32_t)(i < 128 ? tolower(i) : i));
        ST32(p + 52 + 2048 + 4 * i, (uint32_t)(i < 128 ? toupper(i) : i));
    }
    return p;
}
void hle____maskrune(CPU *c) { int ch = (int)c->r[0]; c->r[0] = rune_type(ch) & c->r[1]; }
void hle____memcpy_chk(CPU *c) { memmove(rt_ptr(c->r[0]), rt_ptr(c->r[1]), c->r[2]); }
void hle__calloc(CPU *c) { uint32_t n = c->r[0] * c->r[1]; uint32_t p = rt_malloc(n); memset(rt_ptr(p), 0, n); c->r[0] = p; }
void hle__realloc(CPU *c) {
    uint32_t old = c->r[0], n = c->r[1];
    if (!old) { c->r[0] = rt_malloc(n); return; }
    uint32_t cap = rt_usable_size(old);
    if (n <= cap) return;
    uint32_t p = rt_malloc(n); memcpy(rt_ptr(p), rt_ptr(old), cap); rt_free(old); c->r[0] = p;
}
void hle____divsi3(CPU *c) { int32_t a = (int32_t)c->r[0], b = (int32_t)c->r[1]; c->r[0] = b ? (uint32_t)(a / b) : 0; }
void hle____modsi3(CPU *c) { int32_t a = (int32_t)c->r[0], b = (int32_t)c->r[1]; c->r[0] = b ? (uint32_t)(a % b) : 0; }
void hle____udivsi3(CPU *c) { c->r[0] = c->r[1] ? c->r[0] / c->r[1] : 0; }
void hle____umodsi3(CPU *c) { c->r[0] = c->r[1] ? c->r[0] % c->r[1] : 0; }

// ---- math (double) ---------------------------------------------------------------------------------
static double D0(CPU *c) { uint64_t u = ((uint64_t)c->r[1] << 32) | c->r[0]; double d; memcpy(&d, &u, 8); return d; }
static double D1(CPU *c) { uint64_t u = ((uint64_t)c->r[3] << 32) | c->r[2]; double d; memcpy(&d, &u, 8); return d; }
static void RD(CPU *c, double d) { uint64_t u; memcpy(&u, &d, 8); c->r[0] = (uint32_t)u; c->r[1] = (uint32_t)(u >> 32); }
void hle__sin(CPU *c) { RD(c, sin(D0(c))); }
void hle__cos(CPU *c) { RD(c, cos(D0(c))); }
void hle__atan2(CPU *c) { RD(c, atan2(D0(c), D1(c))); }
void hle__pow(CPU *c) { RD(c, pow(D0(c), D1(c))); }
void hle__ceil(CPU *c) { RD(c, ceil(D0(c))); }
void hle__lround(CPU *c) { c->r[0] = (uint32_t)lround(D0(c)); }
void hle__lroundf(CPU *c) { c->r[0] = (uint32_t)lroundf(F32_BITS(c->r[0])); }

// ---- time / random ---------------------------------------------------------------------------------
void hle__time(CPU *c) { time_t t = time(NULL); if (c->r[0]) ST32(c->r[0], (uint32_t)t); c->r[0] = (uint32_t)t; }
void hle__srand(CPU *c) { srand(c->r[0]); }
void hle__rand(CPU *c) { c->r[0] = (uint32_t)rand(); }
void hle__timegm(CPU *c) { (void)c; c->r[0] = (uint32_t)time(NULL); }
void hle____assert_rtn(CPU *c) { fprintf(stderr, "[rt] assertion failed: %s\n", rt_cstr(c->r[0])); }
void hle__exit(CPU *c) { fprintf(stderr, "[rt] guest called exit(%d)\n", (int)c->r[0]); }
void hle____error(CPU *c) { static uint32_t e; if (!e) e = rt_alloc(8); c->r[0] = e; }

// ---- threads / networking: the revived game has no server, so everything network-shaped fails cleanly ----
// TODO(multiplayer): the online code (BasicSocket / BlossomSocket / Sparkworkz HTTP) is recompiled and present but its OS
// dependencies are stubbed to fail cleanly, so the game falls back to its OfflineSocket. See docs/MULTIPLAYER_TODO.md.
void hle__pthread_create(CPU *c) { c->r[0] = 35; }                    // EAGAIN: the original falls back / reports failure
void hle__pthread_attr_init(CPU *c) { c->r[0] = 0; }
void hle__pthread_attr_destroy(CPU *c) { c->r[0] = 0; }
void hle__pthread_attr_setdetachstate(CPU *c) { c->r[0] = 0; }
void hle__pthread_self(CPU *c) { c->r[0] = 1; }
void hle__pthread_mach_thread_np(CPU *c) { c->r[0] = 1; }
// TODO(multiplayer): BSD sockets (BasicSocket::Connect/Write, recv in the reader thread) — pass through to the host once a server exists.
void hle__socket(CPU *c) { c->r[0] = (uint32_t)-1; }
void hle__connect(CPU *c) { c->r[0] = (uint32_t)-1; }
void hle__send(CPU *c) { c->r[0] = (uint32_t)-1; }
void hle__recv(CPU *c) { c->r[0] = (uint32_t)-1; }
void hle__close(CPU *c) { c->r[0] = 0; }
void hle__shutdown(CPU *c) { c->r[0] = 0; }
void hle__inet_addr(CPU *c) { c->r[0] = 0x0100007f; }
void hle__strptime(CPU *c) { c->r[0] = 0; }
void hle__thread_info(CPU *c) { c->r[0] = 0; }
void hle__thread_policy_set(CPU *c) { c->r[0] = 0; }
void hle_objc_release(uint32_t a);
void hle__CFRelease(CPU *c) { hle_objc_release(c->r[0]); }
void hle__dispatch_async(CPU *c) { (void)c; }

// sscanf: the %d / %f / %s / %x subset the game needs
void hle__sscanf(CPU *c) {
    const char *in = rt_cstr(c->r[0]); const char *fmt = rt_cstr(c->r[1]);
    int reg = 2, matched = 0; uint32_t stack = c->sp;
    #define NEXTPTR() (reg < 4 ? c->r[reg++] : ({ uint32_t v = LD32(stack); stack += 4; v; }))
    for (const char *f = fmt; *f; f++) {
        if (*f == '%') {
            f++; int lng = 0; while (*f == 'l') { lng++; f++; }
            while (*in == ' ') in++;
            if (*f == 'd' || *f == 'i') { char *e; long v = strtol(in, &e, 10); if (e == in) break; ST32(NEXTPTR(), (uint32_t)v); in = e; matched++; }
            else if (*f == 'x') { char *e; long v = strtol(in, &e, 16); if (e == in) break; ST32(NEXTPTR(), (uint32_t)v); in = e; matched++; }
            else if (*f == 'f') { char *e; double v = strtod(in, &e); if (e == in) break; uint32_t p = NEXTPTR(); if (lng) { uint64_t u; memcpy(&u, &v, 8); ST32(p, (uint32_t)u); ST32(p + 4, (uint32_t)(u >> 32)); } else STF(p, (float)v); in = e; matched++; }
            else if (*f == 's') { uint32_t p = NEXTPTR(); int n = 0; while (*in && *in != ' ') ST8(p + n++, (uint8_t)*in++); ST8(p + n, 0); matched++; }
            else break;
        } else if (*f == ' ') { while (*in == ' ') in++; }
        else { if (*in != *f) break; in++; }
    }
    #undef NEXTPTR
    c->r[0] = (uint32_t)matched;
}
