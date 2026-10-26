/* sndhost - see sndhost.h */
#include "sndhost.h"
#include "mixer.h"
#include "iop/iopk.h"
#include "../disc.h"
#include "../plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace sndhost {

namespace {

struct EeServer { uint32_t sid; EeServerFn fn; void *buf; const void *queue, *key; };

Binding g_bind;
SndMixer *g_mix, *g_own_mix;
int g_ready;
int (*g_ee_write)(void *, uint32_t, const void *, uint32_t);
void *g_ee_user;

EeServer g_srv[16];
int g_nsrv;
uint32_t g_waited[32];   /* sids whose first bind already waited (hybrid h_sceSifBindRpc) */
int g_nwaited;
int g_nmods;             /* started modules (the hybrid's nmods) */
int g_auto = 1;
int g_serving;
void (*g_posted)(void *, uint32_t);
void *g_posted_user;
uint64_t g_ee_base;

int ee_write(void *, uint32_t ee_addr, const void *src, uint32_t size)
{
    return g_ee_write ? g_ee_write(g_ee_user, ee_addr, src, size) : 0;
}

int ee_has_server(void *, uint32_t sid)
{
    for (int i = 0; i < g_nsrv; i++) if (g_srv[i].sid == sid) return 1;
    return 0;
}

void ee_rpc_posted(void *, uint32_t sid)
{
    if (g_posted) g_posted(g_posted_user, sid);
}

int strace() { return snd_trace_on && snd_trace_enabled(); }
int g_unnamed;   /* the IRX being started has no module name (trace: "(unnamed)" as the hybrid) */

int serve_one(const EeServer &s)
{
    int n = 0;
    iopk::EeRpcRequest q;
    while (iopk::EeRpcNext(s.sid, &q)) {
        uint32_t ss = q.ssize && q.send && s.buf ? (q.ssize + 15) & ~15u : 0;
        if (ss) memcpy(s.buf, q.send, ss);   /* hybrid iop_ee_serve_one */
        uint32_t scrc = strace() && ss ? disc_crc32(s.buf, ss) : 0;
        void *ret = s.fn(q.fno, s.buf, (int)q.ssize);
        if (strace())   /* runtime/iop_kern.c "IOP RPC" line (the IOP clock: the request's serve time here) */
            snd_trace("IOP", "RPC sid %08x fno %08x mode %x ssize %x rsize %x scrc %08x -> ret %08x rcrc %08x cyc %llu", s.sid,
                      q.fno, q.mode, q.ssize, q.rsize, scrc, (uint32_t)(uintptr_t)ret,
                      ret && q.rsize ? disc_crc32(ret, q.rsize) : 0, (unsigned long long)iopk::Now());
        iopk::EeRpcComplete(q.id, ret);
        n++;
    }
    return n;
}

void after_entry()
{
    if (g_auto) ServePending();
}

int nop_start(const iopk::ModuleArgs *) { return 0; }

/* hybrid load_elf: total = text + data + bss, or the first PT_LOAD's end if larger (iopk::ParseIrx) */
int load_image(const void *elf, uint32_t size, const char *path, const char *args, int argl, int *result)
{
    iopk::IrxInfo ii;
    if (iopk::ParseIrx(elf, size, &ii) != 0) {
        plat_log("sndhost: %s: not an IRX", path);
        return -1;
    }
    const char *name = ii.name[0] ? ii.name : g_bind.name_by_size ? g_bind.name_by_size(ii.total) : 0;
    g_unnamed = !ii.name[0];
    int r = StartModule(name ? name : path, path, ii.total, args, argl, 0, 0, result);
    g_unnamed = 0;
    return r;
}

}  // namespace

/* the audio device's tap (N9): kept across Init / Quit, so it can be set before the game powers the IOP up */
static MixTap g_tap;
static void *g_tap_user;

void Init(const Config *c)
{
    g_bind = *c->bind;
    g_mix = c->mixer;
    if (!g_mix) {
        if (!g_own_mix) g_own_mix = snd_mixer_create();
        g_mix = g_own_mix;
    }
    g_ee_write = c->ee_write;
    g_ee_user = c->ee_user;
    g_nsrv = g_nwaited = g_nmods = 0;
    g_auto = 1;
    g_serving = 0;
    g_ee_base = 0;
    iopk::Hooks h;
    memset(&h, 0, sizeof h);
    h.ee_write = c->ee_write ? ee_write : 0;
    h.ee_has_server = ee_has_server;
    h.ee_rpc_posted = ee_rpc_posted;
    g_bind.init(g_mix, &h);
    if (g_tap && g_bind.set_mix_tap) g_bind.set_mix_tap(g_tap, g_tap_user);   /* a tap set before power-on (N9) */
    g_ready = 1;
}

void Quit()
{
    iopk::SetHooks(0);
    if (g_bind.set_mix_tap) g_bind.set_mix_tap(0, 0);
    g_nsrv = g_nwaited = 0;
    g_posted = 0;
    g_ready = 0;
}

int Ready() { return g_ready; }
SndMixer *Mixer() { return g_mix; }

uint32_t AllocIopHeap(uint32_t size) { return iopk::AllocSysMemory(iopk::ALLOC_FIRST, size, 0); }
int FreeIopHeap(uint32_t addr) { return iopk::FreeSysMemory(addr); }
int DmaToIop(const void *src, uint32_t iop_dst, uint32_t size) { return iopk::EeDmaToIop(src, iop_dst, size); }

int StartModule(const char *name, const char *path, uint32_t image_size, const char *args, int argl, StartFn fn,
                uint32_t *base, int *result)
{
    if (result) *result = 0;
    if (!fn) {
        uint32_t sz = 0;
        fn = g_bind.resolve ? g_bind.resolve(name, &sz) : 0;
        if (!fn && !sz) {
            plat_log("sndhost: module %s (%s) not run", name, path);
            if (strace()) snd_trace("IOP", "MOD skip %s (%s)", name, path);   /* hybrid: + crc */
            return 1000 + g_nmods;
        }
        if (!fn) fn = nop_start;
    }
    uint32_t got = 0;
    int res = 0;
    int idx = iopk::StartModule(name, path, image_size, args, argl, fn, 0, &got, &res);
    if (base) *base = got;
    if (idx < 0) {
        plat_log("sndhost: module %s (%s) failed to start", name, path);
        return -1;
    }
    if (result) *result = res;
    if (strace())   /* hybrid "IOP MOD load" without entry / text / crc (the native image is a phantom) */
        snd_trace("IOP", "MOD load %s (%s) at %06x size %x args %d cyc %llu", g_unnamed ? "(unnamed)" : name, path, got,
                  image_size, argl, (unsigned long long)iopk::Now());
    g_nmods++;
    after_entry();
    return g_nmods;
}

int LoadModuleBuffer(uint32_t iop_addr, const char *args, int argl, int *result)
{
    uint8_t *p = iopk::Ptr(iop_addr);
    if (!p) return -1;
    char path[32];
    snprintf(path, sizeof path, "iopmem:%06x", iop_addr);
    return load_image(p, iopk::RAM_SIZE - (iop_addr & (iopk::RAM_SIZE - 1)), path, args, argl, result);
}

int LoadModuleFile(const char *path, const char *args, int argl, int *result)
{
    const char *p = strchr(path, ':');
    uint32_t lsn, size;
    if (!p || !disc_find(p + 1, &lsn, &size)) {
        plat_log("sndhost: module %s not found", path);
        return -1;
    }
    uint8_t *buf = (uint8_t *)malloc(size + 2048);
    int ok = buf && disc_read(lsn, (size + 2047) / 2048, buf) == 0;
    int r = ok ? load_image(buf, size, path, args, argl, result) : -1;
    free(buf);
    return r;
}

int Bind(uint32_t sid)
{
    if (iopk::HaveServer(sid)) return 1;
    for (int k = 0; k < g_nwaited; k++) if (g_waited[k] == sid) return 0;
    if (g_nwaited < 32) g_waited[g_nwaited++] = sid;
    int ok = iopk::WaitServer(sid);
    after_entry();
    return ok;
}

int RpcCall(uint32_t sid, uint32_t fno, const void *send, uint32_t ssize, void *recv, uint32_t rsize)
{
    int r = iopk::RpcCall(sid, fno, send, ssize, recv, rsize);
    after_entry();
    return r;
}

int RegisterEeServer(uint32_t sid, EeServerFn fn, void *buf, const void *queue, const void *key)
{
    int i = 0;
    while (i < g_nsrv && g_srv[i].sid != sid) i++;
    if (i == g_nsrv) {
        if (g_nsrv == 16) { plat_log("sndhost: EE server table full (sid %08x)", sid); return -1; }
        g_nsrv++;
    }
    g_srv[i] = EeServer{sid, fn, buf, queue, key};
    return 0;
}

void RemoveEeServer(const void *key)
{
    for (int i = 0; i < g_nsrv; i++)
        if (g_srv[i].key == key) {
            g_srv[i] = g_srv[--g_nsrv];
            return;
        }
}

int ServeQueue(const void *queue)
{
    if (g_serving) return 0;   /* an EE server function calling back into the host */
    g_serving = 1;
    int n = 0;
    for (int i = 0; i < g_nsrv; i++)
        if (g_srv[i].fn && g_srv[i].queue == queue) n += serve_one(g_srv[i]);
    g_serving = 0;
    return n;
}

int ServePending()
{
    if (g_serving) return 0;
    g_serving = 1;
    int n = 0;
    for (int i = 0; i < g_nsrv; i++)
        if (g_srv[i].fn) n += serve_one(g_srv[i]);
    g_serving = 0;
    return n;
}

void SetAutoServe(int on) { g_auto = on; }

void SetPostedHook(void (*fn)(void *, uint32_t), void *user)
{
    g_posted = fn;
    g_posted_user = user;
}

uint64_t Now() { return iopk::Now(); }

void Advance(uint64_t iop_target)
{
    if (iop_target > iopk::Now()) iopk::Advance(iop_target);
    after_entry();
}

void AdvanceEe(uint64_t ee_cycles) { Advance(ee_cycles / 8 + g_ee_base); }
void SetEeBase(uint64_t iop_cycles) { g_ee_base = iop_cycles; }

void SetMixTap(MixTap fn, void *user)
{
    g_tap = fn;
    g_tap_user = user;
    if (g_bind.set_mix_tap) g_bind.set_mix_tap(fn, user);
}

}  // namespace sndhost
