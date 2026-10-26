/* audiodev - mixer tap -> bounded ring -> SDL3 audio stream (see audiodev.h). The ring is guarded by the SDL stream's
 * own lock (SDL_LockAudioStream: the device callback runs with it held), so producer and device never race and the
 * producer never waits on anything but that short critical section. */
#include "audiodev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sdl3.h"
#include "../plat.h"
#include "../snd/sndhost.h"

namespace {

const int RATE = 48000;
const int VB = 801;                  /* frames per vblank (48000 / 59.94) */
const int PRIME = VB * 3 / 2;
const int HIGH = VB * 4;
const int TRIM = VB * 2;
const int RING = 8192;               /* frames, power of two, > HIGH + one mixer chunk */

int g_enable = 1, g_volume = 100;
int g_open, g_tap_on, g_closed;
SDL_AudioStream *g_stream;
const uint32_t *g_vb;

int16_t g_ring[RING * 2];
uint32_t g_rd, g_wr;                 /* frame counters (wrap mod 2^32, RING divides it) */
int g_playing;                       /* 0 = priming */
uint64_t g_queued, g_played, g_silence, g_dropped;
unsigned g_underruns, g_overruns, g_ovlog;   /* overruns at vblank 0 = the boot burst (IOP runs ahead in the binds), not logged */
uint32_t g_first_vb = 0xffffffffu;
volatile uint32_t g_under_vb;        /* device thread: the sim's vblank at the last underrun (a racy read, log only) */

struct Wav {
    FILE *f;
    uint32_t frames;
};
Wav g_dump, g_ddump;

void wav_header(Wav *w)
{
    uint32_t data = w->frames * 4, riff = 36 + data, fmt = 16, rate = RATE, bps = RATE * 4;
    uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    fseek(w->f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, w->f); fwrite(&riff, 4, 1, w->f); fwrite("WAVEfmt ", 1, 8, w->f); fwrite(&fmt, 4, 1, w->f);
    fwrite(&pcm, 2, 1, w->f); fwrite(&ch, 2, 1, w->f); fwrite(&rate, 4, 1, w->f); fwrite(&bps, 4, 1, w->f);
    fwrite(&align, 2, 1, w->f); fwrite(&bits, 2, 1, w->f); fwrite("data", 1, 4, w->f); fwrite(&data, 4, 1, w->f);
    fseek(w->f, 0, SEEK_END);
}
void wav_open(Wav *w, const char *env)
{
    const char *p = getenv(env);
    if (!p || !*p) return;
    w->f = fopen(p, "wb");
    if (!w->f) { plat_log("audio: %s: cannot write %s", env, p); return; }
    w->frames = 0;
    wav_header(w);
    plat_log("audio: %s -> %s", env, p);
}
void wav_write(Wav *w, const int16_t *lr, int frames)
{
    if (!w->f || frames <= 0) return;
    fwrite(lr, 4, (size_t)frames, w->f);
    w->frames += (uint32_t)frames;
}
void wav_close(Wav *w)
{
    if (!w->f) return;
    wav_header(w);
    fclose(w->f);
    w->f = 0;
}

/* sim thread (inside sndhost's IOP clock): the mixer's frames, never blocks on the device beyond the stream lock */
void tap(void *, const int16_t *lr, int frames)
{
    int16_t v[2 * 256];
    if (frames > 256) frames = 256;   /* iophost mixes in chunks of <= 256 */
    for (int i = 0; i < 2 * frames; i++) {
        int s = lr[i] * g_volume / 100;
        v[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
    if (g_first_vb == 0xffffffffu) {
        for (int i = 0; i < 2 * frames; i++)
            if (v[i]) { g_first_vb = g_vb ? *g_vb : 0; break; }
    }
    wav_write(&g_dump, v, frames);
    g_queued += (uint64_t)frames;
    uint32_t ov_vb = 0, ov_n = 0;
    if (!g_stream) return;
    sdl3.SDL_LockAudioStream(g_stream);
    for (int i = 0; i < frames; i++) {
        uint32_t p = (g_wr % RING) * 2;
        g_ring[p] = v[2 * i];
        g_ring[p + 1] = v[2 * i + 1];
        g_wr++;
    }
    if (g_wr - g_rd > (uint32_t)HIGH) {   /* overrun: drop the oldest */
        uint32_t n = g_wr - g_rd - TRIM;
        g_rd += n;
        g_dropped += n;
        g_overruns++;
        ov_vb = g_vb ? *g_vb : 0;
        ov_n = n;
    }
    sdl3.SDL_UnlockAudioStream(g_stream);
    if (ov_n && ov_vb && ++g_ovlog <= 8) plat_log("audio: overrun %u at vblank %u: dropped the oldest %u frames", g_overruns, ov_vb, ov_n);
}

/* SDL's device thread, stream locked: hand over what the ring has, silence for the rest (never touches sim state) */
void SDLCALL pull(void *, SDL_AudioStream *s, int additional, int)
{
    static int16_t buf[2 * 1024];
    int want = additional / 4;
    while (want > 0) {
        int n = want > 1024 ? 1024 : want;
        uint32_t avail = g_wr - g_rd;
        if (!g_playing && avail >= (uint32_t)PRIME) g_playing = 1;
        int got = 0;
        if (g_playing) {
            got = (int)(avail < (uint32_t)n ? avail : (uint32_t)n);
            for (int i = 0; i < got; i++) {
                uint32_t p = ((g_rd + (uint32_t)i) % RING) * 2;
                buf[2 * i] = g_ring[p];
                buf[2 * i + 1] = g_ring[p + 1];
            }
            g_rd += (uint32_t)got;
            g_played += (uint64_t)got;
            if (got < n) { g_playing = 0; g_underruns++; g_under_vb = g_vb ? *g_vb : 0; }
        }
        if (got < n) {
            memset(buf + 2 * got, 0, (size_t)(n - got) * 4);
            g_silence += (uint64_t)(n - got);
        }
        wav_write(&g_ddump, buf, n);
        sdl3.SDL_PutAudioStreamData(s, buf, n * 4);
        want -= n;
    }
}

int open_device()
{
    if (!sdl3_audio_ok()) { plat_log("audio: SDL3 audio unavailable - silent"); return 0; }
    sdl3.SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "480");
    if (!sdl3.SDL_InitSubSystem(SDL_INIT_AUDIO)) { plat_log("audio: no audio subsystem (%s) - silent", sdl3.SDL_GetError()); return 0; }
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = 2;
    spec.freq = RATE;
    g_stream = sdl3.SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, pull, 0);
    if (!g_stream) {
        plat_log("audio: no output device (%s) - silent", sdl3.SDL_GetError());
        sdl3.SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }
    SDL_AudioDeviceID dev = sdl3.SDL_GetAudioStreamDevice(g_stream);
    SDL_AudioSpec ds;
    int frames = 0;
    if (!sdl3.SDL_GetAudioDeviceFormat(dev, &ds, &frames)) ds.freq = 0;
    const char *name = sdl3.SDL_GetAudioDeviceName(dev);
    plat_log("audio: SDL3 %s '%s' (device %d Hz, %d frames), 48 kHz stereo s16, volume %d",
             sdl3.SDL_GetCurrentAudioDriver() ? sdl3.SDL_GetCurrentAudioDriver() : "?", name ? name : "?", ds.freq, frames,
             g_volume);
    sdl3.SDL_ResumeAudioStreamDevice(g_stream);
    return 1;
}

}  // namespace

int audiodev_set(const char *kv)
{
    const char *eq = strchr(kv, '=');
    if (!eq) return 0;
    size_t n = (size_t)(eq - kv);
    const char *v = eq + 1;
    if ((n == 12 && !strncmp(kv, "audio.enable", n)) || (n == 13 && !strncmp(kv, "audio.enabled", n))) {
        g_enable = atoi(v) != 0;
        return 1;
    }
    if (n == 12 && !strncmp(kv, "audio.volume", n)) {
        int x = atoi(v);
        g_volume = x < 0 ? 0 : x > 200 ? 200 : x;   /* runtime/audio.c range */
        return 1;
    }
    return 0;
}

void audiodev_open(int headless, const uint32_t *vblank)
{
    if (g_open) return;
    g_open = 1;
    g_vb = vblank;
    wav_open(&g_dump, "ACNB_AUDIO_DUMP");
    if (!headless && g_enable) {
        if (open_device()) wav_open(&g_ddump, "ACNB_AUDIO_DUMP_DEV");
    } else {
        plat_log("audio: off (%s)", headless ? "headless / mute" : "audio.enable=0");
    }
    if (g_stream || g_dump.f) {
        sndhost::SetMixTap(tap, 0);
        g_tap_on = 1;
    }
}

void audiodev_close(void)
{
    if (!g_open || g_closed) return;
    g_closed = 1;
    if (g_tap_on) sndhost::SetMixTap(0, 0);
    if (g_stream) {
        sdl3.SDL_DestroyAudioStream(g_stream);   /* stops the device thread first: no pull after this */
        g_stream = 0;
        sdl3.SDL_QuitSubSystem(SDL_INIT_AUDIO);
        plat_log("audio: queued %llu frames, played %llu, silence %llu, dropped %llu; underruns %u (last at "
                 "vblank %u), overruns %u; first audible vblank %d", (unsigned long long)g_queued,
                 (unsigned long long)g_played, (unsigned long long)g_silence, (unsigned long long)g_dropped, g_underruns,
                 (unsigned)g_under_vb, g_overruns,
                 g_first_vb == 0xffffffffu ? -1 : (int)g_first_vb);
    } else if (g_tap_on) {
        plat_log("audio: queued %llu frames (dump only), first audible vblank %d", (unsigned long long)g_queued,
                 g_first_vb == 0xffffffffu ? -1 : (int)g_first_vb);
    }
    wav_close(&g_dump);
    wav_close(&g_ddump);
}

/* --audio-test: a recorded mixer WAV (48 kHz stereo s16, e.g. a hybrid --wav) through the same tap -> ring -> SDL
 * path, produced at the game's rate (800.8 frames per 59.94 Hz vblank, chunks of 256 like iophost, paced on host
 * time like the video pacer). Starts 0.5 s before the first non-zero frame. With ACNB_AUDIO_DUMP_DEV the device
 * stream is recorded for a sample compare (dev = input after the prime silence when no drop / underrun happened). */
int audiodev_test(const char *path, unsigned vblanks)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "audio-test: cannot open %s\n", path); return 2; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 44) { fclose(f); return 2; }
    uint32_t n = (uint32_t)((size - 44) / 4);
    int16_t *pcm = (int16_t *)malloc((size_t)n * 4 + 4);
    fseek(f, 44, SEEK_SET);
    n = (uint32_t)fread(pcm, 4, n, f);
    fclose(f);
    uint32_t pos = 0;
    while (pos < n && !pcm[2 * pos] && !pcm[2 * pos + 1]) pos++;
    pos = pos > (uint32_t)RATE / 2 ? pos - (uint32_t)RATE / 2 : 0;
    uint32_t vb = 0;
    g_vb = &vb;   /* first-audible clock = test vblanks */
    g_open = 1;
    wav_open(&g_dump, "ACNB_AUDIO_DUMP");
    if (!open_device()) { free(pcm); return 3; }
    wav_open(&g_ddump, "ACNB_AUDIO_DUMP_DEV");
    fprintf(stderr, "audio-test: %s, %u frames from frame %u, %u vblanks\n", path, n - pos, pos, vblanks);
    uint64_t t0 = sdl3.SDL_GetTicksNS(), acc = 0;
    for (vb = 0; vb < vblanks && pos < n; vb++) {
        acc += 48000ull * 1001;
        uint32_t frames = (uint32_t)(acc / 60000);
        acc %= 60000;
        while (frames && pos < n) {
            uint32_t c = frames > 256 ? 256 : frames;
            if (c > n - pos) c = n - pos;
            tap(0, pcm + 2 * pos, (int)c);
            pos += c;
            frames -= c;
        }
        uint64_t due = t0 + (uint64_t)(vb + 1) * 1001000000ull / 60;   /* 59.94 Hz */
        uint64_t now = sdl3.SDL_GetTicksNS();
        if (due > now) sdl3.SDL_DelayPrecise(due - now);
    }
    sdl3.SDL_DelayPrecise(100000000ull);   /* let the device drain the ring */
    audiodev_close();
    free(pcm);
    return 0;
}
