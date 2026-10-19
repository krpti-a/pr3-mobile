// Audio: the original used Apple's SoundEngine sample (AudioFile + OpenAL). OpenAL is forwarded to the host;
// AudioFile is implemented on top of the game's PCM WAV files.
#include "arm_rt.h"
#if defined(RT_AUDIO_REAL)
  #if defined(__APPLE__)
    #include <OpenAL/al.h>
    #include <OpenAL/alc.h>
  #endif
#else
  typedef int ALint; typedef unsigned int ALuint; typedef int ALsizei; typedef int ALenum;
#endif

uint32_t hle_objc_string_value(uint32_t obj, char *out, int cap);
uint32_t hle_objc_new_string(const char *s);

// ---- AudioSession ------------------------------------------------------------------------
void hle__AudioSessionInitialize(CPU *c) { c->r[0] = 0; }
void hle__AudioSessionSetProperty(CPU *c) { c->r[0] = 0; }
void hle__AudioSessionSetActive(CPU *c) { c->r[0] = 0; }

// ---- CFURL (paths only) ----------------------------------------------------------------------
void hle__CFURLCreateFromFileSystemRepresentation(CPU *c) {     // (allocator, const UInt8 *buffer, CFIndex len, Boolean isDir)
    char buf[1024]; uint32_t n = c->r[2] < 1023 ? c->r[2] : 1023;
    memcpy(buf, rt_ptr(c->r[1]), n); buf[n] = 0;
    c->r[0] = hle_objc_new_string(buf);
}

// ---- AudioFile on WAV ----------------------------------------------------------------------------
typedef struct { int used; FILE *f; uint32_t data_off, data_len; uint16_t channels, bits; uint32_t rate; } WavFile;
static WavFile wavs[32];
extern char rt_resource_root[1024];

void hle__AudioFileOpenURL(CPU *c) {            // (CFURLRef, perm, typeHint, AudioFileID *out)
    char path[1024]; hle_objc_string_value(c->r[0], path, sizeof path);
    char full[2048];
    if (path[0] == '/') snprintf(full, sizeof full, "%s", path); else snprintf(full, sizeof full, "%s/%s", rt_resource_root, path);
    FILE *f = fopen(full, "rb");
    if (!f) { const char *b = strrchr(path, '/'); if (b) { snprintf(full, sizeof full, "%s/%s", rt_resource_root, b + 1); f = fopen(full, "rb"); } }
    c->r[0] = (uint32_t)-43;                     // fnfErr
    if (!f) return;
    uint8_t hdr[12]; if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) { fclose(f); c->r[0] = (uint32_t)-1; return; }
    WavFile w; memset(&w, 0, sizeof w); w.f = f; w.used = 1;
    for (;;) {
        uint8_t ch[8]; if (fread(ch, 1, 8, f) != 8) break;
        uint32_t len = ch[4] | ch[5] << 8 | ch[6] << 16 | (uint32_t)ch[7] << 24;
        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fm[16]; if (fread(fm, 1, 16, f) != 16) break;
            w.channels = fm[2] | fm[3] << 8; w.rate = fm[4] | fm[5] << 8 | fm[6] << 16 | (uint32_t)fm[7] << 24; w.bits = fm[14] | fm[15] << 8;
            fseek(f, (long)len - 16 + (len & 1), SEEK_CUR);
        } else if (!memcmp(ch, "data", 4)) { w.data_off = (uint32_t)ftell(f); w.data_len = len; break; }
        else fseek(f, (long)len + (len & 1), SEEK_CUR);
    }
    for (int i = 0; i < 32; i++) if (!wavs[i].used) { wavs[i] = w; ST32(c->r[3], (uint32_t)(i + 1)); c->r[0] = 0; return; }
    fclose(f);
}
void hle__AudioFileClose(CPU *c) { int i = (int)c->r[0] - 1; if (i >= 0 && i < 32 && wavs[i].used) { fclose(wavs[i].f); wavs[i].used = 0; } c->r[0] = 0; }
void hle__AudioFileGetPropertyInfo(CPU *c) { if (c->r[2]) ST32(c->r[2], 40); if (c->r[3]) ST32(c->r[3], 0); c->r[0] = 0; }
void hle__AudioFileGetProperty(CPU *c) {        // (file, propertyID, UInt32 *ioDataSize, void *out)
    int i = (int)c->r[0] - 1; uint32_t prop = c->r[1];
    c->r[0] = (uint32_t)-1;
    if (i < 0 || i >= 32 || !wavs[i].used) return;
    WavFile *w = &wavs[i];
    if (prop == 0x64666d74) {                   // 'dfmt' AudioStreamBasicDescription
        double rate = w->rate; uint64_t u; memcpy(&u, &rate, 8);
        uint32_t out = c->r[3];
        ST32(out, (uint32_t)u); ST32(out + 4, (uint32_t)(u >> 32));
        ST32(out + 8, 0x6c70636d);              // 'lpcm'
        ST32(out + 12, 0x0c);                   // little-endian, packed, signed integer
        uint32_t bpf = w->channels * (w->bits / 8);
        ST32(out + 16, bpf); ST32(out + 20, 1); ST32(out + 24, bpf); ST32(out + 28, w->channels); ST32(out + 32, w->bits); ST32(out + 36, 0);
        if (c->r[2]) ST32(c->r[2], 40);
        c->r[0] = 0;
    } else if (prop == 0x62636e74) {            // 'bcnt' audio data byte count (UInt64)
        ST32(c->r[3], w->data_len); ST32(c->r[3] + 4, 0);
        if (c->r[2]) ST32(c->r[2], 8);
        c->r[0] = 0;
    }
}
void hle__AudioFileReadBytes(CPU *c) {          // (file, useCache, SInt64 startByte [r2:r3], UInt32 *ioNumBytes [stack], void *out [stack])
    int i = (int)c->r[0] - 1; uint64_t start = ((uint64_t)c->r[3] << 32) | c->r[2];
    uint32_t pn = LD32(c->sp), out = LD32(c->sp + 4);
    c->r[0] = (uint32_t)-1;
    if (i < 0 || i >= 32 || !wavs[i].used) return;
    WavFile *w = &wavs[i];
    uint32_t want = LD32(pn);
    if (start >= w->data_len) { ST32(pn, 0); c->r[0] = 0; return; }
    if (want > w->data_len - start) want = (uint32_t)(w->data_len - start);
    fseek(w->f, (long)(w->data_off + start), SEEK_SET);
    uint32_t got = (uint32_t)fread(rt_ptr(out), 1, want, w->f);
    ST32(pn, got); c->r[0] = 0;
}
void hle__AudioFileReadPackets(CPU *c) { c->r[0] = (uint32_t)-1; }
// AudioQueue (streamed music) is not used by the bundled content
void hle__AudioQueueNewOutput(CPU *c) { c->r[0] = (uint32_t)-1; }

// ---- OpenAL -------------------------------------------------------------------------------------------
#if defined(RT_AUDIO_REAL)
#define AL(stmt) do { stmt; } while (0)
#else
#define AL(stmt) do { } while (0)
#endif
static uint32_t next_id = 1;
#if defined(RT_AUDIO_REAL)
static ALCdevice *g_dev; static ALCcontext *g_ctx;
void hle__alcOpenDevice(CPU *c) { if (!g_dev) g_dev = alcOpenDevice(NULL); c->r[0] = g_dev ? 0x1d000001 : 0; }
void hle__alcCreateContext(CPU *c) {
    if (!g_dev) g_dev = alcOpenDevice(NULL);
    g_ctx = g_dev ? alcCreateContext(g_dev, NULL) : NULL;
    c->r[0] = g_ctx ? 0x1c000001 : 0;
}
void hle__alcMakeContextCurrent(CPU *c) { if (g_ctx && c->r[0]) { alcMakeContextCurrent(g_ctx); c->r[0] = 1; } else { alcMakeContextCurrent(NULL); c->r[0] = 1; } }
void hle__alcDestroyContext(CPU *c) { (void)c; }
void hle__alcCloseDevice(CPU *c) { c->r[0] = 1; }
void hle__alGetError(CPU *c) { c->r[0] = (uint32_t)alGetError(); }
void hle__alGenBuffers(CPU *c) { ALuint ids[64]; ALsizei n = (ALsizei)c->r[0]; if (n > 64) n = 64; alGenBuffers(n, ids); for (int i = 0; i < n; i++) ST32(c->r[1] + 4 * i, ids[i]); }
static int g_srcs; void hle__alGenSources(CPU *c) { ALuint ids[64]; ALsizei n = (ALsizei)c->r[0]; if (n > 64) n = 64; alGenSources(n, ids); g_srcs += n; if (getenv("PR3_AUDIOLOG")) fprintf(stderr, "[al] genSources(%d) -> %u, total %d, err 0x%x\n", n, ids[0], g_srcs, alGetError()); for (int i = 0; i < n; i++) ST32(c->r[1] + 4 * i, ids[i]); }
void hle__alDeleteSources(CPU *c) { ALuint ids[64]; ALsizei n = (ALsizei)c->r[0]; if (n > 64) n = 64; for (int i = 0; i < n; i++) ids[i] = LD32(c->r[1] + 4 * i); alDeleteSources(n, ids); }
void hle__alGetSourcei(CPU *c) { ALint v = 0; alGetSourcei(c->r[0], c->r[1], &v); ST32(c->r[2], (uint32_t)v); }
void hle__alSourcei(CPU *c) { alSourcei(c->r[0], c->r[1], (ALint)c->r[2]); }
void hle__alSourcePlay(CPU *c) { if (getenv("PR3_AUDIOLOG")) { ALint buf = 0, st = 0; alGetSourcei(c->r[0], AL_BUFFER, &buf); alGetSourcei(c->r[0], AL_SOURCE_STATE, &st); fprintf(stderr, "[al] play source %u buffer %d (state %d, err 0x%x)\n", c->r[0], buf, st, alGetError()); } alSourcePlay(c->r[0]); }
void hle__alSourceStop(CPU *c) { alSourceStop(c->r[0]); }
void hle__alSourceQueueBuffers(CPU *c) { ALuint ids[64]; ALsizei n = (ALsizei)c->r[1]; if (n > 64) n = 64; for (int i = 0; i < n; i++) ids[i] = LD32(c->r[2] + 4 * i); alSourceQueueBuffers(c->r[0], n, ids); }
void hle__alSourceUnqueueBuffers(CPU *c) { ALuint ids[64]; ALsizei n = (ALsizei)c->r[1]; if (n > 64) n = 64; alSourceUnqueueBuffers(c->r[0], n, ids); for (int i = 0; i < n; i++) ST32(c->r[2] + 4 * i, ids[i]); }
static void fn_alBufferDataStatic(CPU *c) {     // (buffer, format, data, size, freq[stack])
    alBufferData(c->r[0], (ALenum)c->r[1], rt_ptr(c->r[2]), (ALsizei)c->r[3], (ALsizei)LD32(c->sp));
}
#else
void hle__alcOpenDevice(CPU *c) { c->r[0] = 0x1d000001; }
void hle__alcCreateContext(CPU *c) { c->r[0] = 0x1c000001; }
void hle__alcMakeContextCurrent(CPU *c) { c->r[0] = 1; }
void hle__alcDestroyContext(CPU *c) { (void)c; }
void hle__alcCloseDevice(CPU *c) { c->r[0] = 1; }
void hle__alGetError(CPU *c) { c->r[0] = 0; }
void hle__alGenBuffers(CPU *c) { for (uint32_t i = 0; i < c->r[0]; i++) ST32(c->r[1] + 4 * i, next_id++); }
void hle__alGenSources(CPU *c) { for (uint32_t i = 0; i < c->r[0]; i++) ST32(c->r[1] + 4 * i, next_id++); }
void hle__alDeleteSources(CPU *c) { (void)c; }
void hle__alGetSourcei(CPU *c) { ST32(c->r[2], 0); }
void hle__alSourcei(CPU *c) { (void)c; }
void hle__alSourcePlay(CPU *c) { (void)c; }
void hle__alSourceStop(CPU *c) { (void)c; }
void hle__alSourceQueueBuffers(CPU *c) { (void)c; }
void hle__alSourceUnqueueBuffers(CPU *c) { for (uint32_t i = 0; i < c->r[1]; i++) ST32(c->r[2] + 4 * i, 0); }
static void fn_alBufferDataStatic(CPU *c) { (void)c; }
#endif

// alcGetProcAddress("alBufferDataStatic") hands the guest a function pointer; guest code calls it via blx.
#define HLE_FN_BASE 0x0ff00000u
void hle__alcGetProcAddress(CPU *c) {
    const char *n = rt_cstr(c->r[1]);
    c->r[0] = !strcmp(n, "alBufferDataStatic") ? HLE_FN_BASE : 0;
}
int rt_hle_function(CPU *c, uint32_t addr) {
    if (addr == HLE_FN_BASE) { fn_alBufferDataStatic(c); return 1; }
    return 0;
}
