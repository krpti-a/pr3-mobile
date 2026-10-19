// Native replacements for the hot spots of the original's PNG loader (LodePNG, recompiled): every sprite is decoded with it, which
// made loading a level or the menus take seconds on a phone. They behave like the originals (same arguments, results and error
// semantics where it matters) but run as plain C: zlib's inflate for LodeFlate_inflate, a byte loop for unfilter / adler32.
// The original's ucvector grew with realloc(size * 8) per step (up to 16 bytes of heap per output byte); the result is now exact.
#include "arm_rt.h"
#include <zlib.h>

// LodeFlate_inflate(ucvector *out {data, size, allocsize}, const uchar *in, size_t insize, size_t inpos) -> error code (0 = ok)
void override_LodeFlate_inflate(CPU *c) {
    uint32_t vec = c->r[0], in = c->r[1], size = c->r[2], pos = c->r[3];
    c->r[0] = 0x34;
    if (pos >= size) return;
    z_stream zs; memset(&zs, 0, sizeof zs);
    if (inflateInit2(&zs, -15) != Z_OK) { c->r[0] = 0x33; return; }
    zs.next_in = rt_ptr(in + pos); zs.avail_in = size - pos;
    size_t cap = (size_t)(size - pos) * 4 + 65536, len = 0;
    uint8_t *buf = malloc(cap);
    int rc;
    for (;;) {
        zs.next_out = buf + len; zs.avail_out = (uInt)(cap - len);
        rc = inflate(&zs, Z_NO_FLUSH);
        len = cap - zs.avail_out;
        if (rc != Z_OK && rc != Z_BUF_ERROR) break;
        if (zs.avail_out == 0) { cap *= 2; buf = realloc(buf, cap); }
        else if (zs.avail_in == 0) break;                 // truncated input
    }
    inflateEnd(&zs);
    if (rc != Z_STREAM_END) { free(buf); c->r[0] = (rc == Z_DATA_ERROR) ? 0x12 : 0x34; return; }
    uint32_t old = LD32(vec);
    uint32_t data = rt_malloc((uint32_t)len ? (uint32_t)len : 1);
    memcpy(rt_ptr(data), buf, len); free(buf);
    if (old) rt_free(old);
    ST32(vec, data); ST32(vec + 4, (uint32_t)len); ST32(vec + 8, (uint32_t)len);
    c->r[0] = 0;
}

// adler32(const uchar *data, unsigned len)
void override_adler32(CPU *c) {
    const uint8_t *p = rt_ptr(c->r[0]); uint32_t len = c->r[1], a = 1, b = 0;
    while (len) {
        uint32_t n = len < 5552 ? len : 5552; len -= n;
        while (n--) { a += *p++; b += a; }
        a %= 65521; b %= 65521;
    }
    c->r[0] = (b << 16) | a;
}

static inline int paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

// unfilter(uchar *out, const uchar *in, unsigned w, unsigned h, unsigned bpp) -> 0 or 36 (bad filter type). `in` has a filter-type byte
// before every scanline, `out` none.
void override_unfilter(CPU *c) {
    uint8_t *out = rt_ptr(c->r[0]); const uint8_t *in = rt_ptr(c->r[1]);
    uint32_t w = c->r[2], h = c->r[3], bpp = LD32(c->sp);
    uint32_t bw = (bpp + 7) >> 3, lb = (w * bpp + 7) >> 3;
    const uint8_t *prev = NULL;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t ft = *in++;
        switch (ft) {
        case 0: memcpy(out, in, lb); break;
        case 1: for (uint32_t i = 0; i < lb; i++) out[i] = in[i] + (i >= bw ? out[i - bw] : 0); break;
        case 2: for (uint32_t i = 0; i < lb; i++) out[i] = in[i] + (prev ? prev[i] : 0); break;
        case 3: for (uint32_t i = 0; i < lb; i++) out[i] = in[i] + (((i >= bw ? out[i - bw] : 0) + (prev ? prev[i] : 0)) >> 1); break;
        case 4: for (uint32_t i = 0; i < lb; i++) out[i] = in[i] + paeth(i >= bw ? out[i - bw] : 0, prev ? prev[i] : 0, (prev && i >= bw) ? prev[i - bw] : 0); break;
        default: c->r[0] = 0x24; return;
        }
        prev = out; out += lb; in += lb;
    }
    c->r[0] = 0;
}
