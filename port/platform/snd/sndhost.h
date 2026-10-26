/* sndhost: the product sound host (phase 3 step 5 NI9) - the one place the EE side of the game meets the native IOP.
 * Owns (or borrows) the SPU2 mixer, powers the native IOP up through a game binding (ACNB: iophost::HostBinding(), which
 * binds iopk + native sd + the mixer and wires the native hsyn / midi interfaces), and gives the EE's SIF layer what the
 * PS2 IOP gave it: module loads, the IOP heap, EE -> IOP DMA, SIF RPC calls, the EE RPC server registry for IOP -> EE
 * requests, and the clock entry. Generic (no game addresses, no module names; Last Raven reusable): the game binding
 * maps IRX module names onto native starts. Users: lib/sdk/libsif.cpp (source port SIF / libsdr), --snd-selftest.
 * One host per process; not thread-safe (sim thread only); no entry from inside an IOP thread or handler.
 *
 * Heap address model: AllocIopHeap returns an IOP ADDRESS (iopk sysmem, the same first-fit heap the native modules
 * allocate from, so the EE's load buffers / bank buffers land where the hybrid's iop_alloc puts them). The EE code only
 * passes it on (RPC payload words, SifSetDma destinations, FreeIopHeap); it never dereferences it.
 *
 * Module model: an IRX image (in IOP RAM after the EE's SetDma, or a cdrom0: file) is parsed for its module name and
 * size; a name the binding knows starts its native module_start on a phantom image of that size (iopk::StartModule,
 * hybrid load_elf order: image, argv block, start thread); any other module is "not run" (logged, result 0, id 1000 +
 * n: the hybrid's skip list). Ids of started modules count from 1 like the hybrid's load_elf.
 *
 * RPC serve model: EE -> IOP calls run to completion inside
 * RpcCall (the mode's no-wait bit changes nothing: the call is done when it returns, CheckStatRpc = 0, as the hybrid's
 * HLE). IOP -> EE requests (an IOP module's SifCallRpc to an EE server) are posted by iopk; the ee_rpc_posted hook only
 * marks them (it runs on an IOP thread: never serve or switch EE threads there). With auto-serve on (default) the host
 * serves every pending request of a registered EE server right after each host entry (RpcCall / Advance / module
 * load): send bytes -> the server buffer, server function, reply -> the IOP caller. The boot slice's EE thread model
 * turns auto-serve off and runs each server thread as `for (;;) { sndhost::ServeQueue(qd); SleepThread(); }`
 * (= sceSifRpcLoop), woken from the posted hook (SetPostedHook). A server registered with fn 0 is "external": the
 * caller serves it through iopk::EeRpcNext / EeRpcComplete (--snd-selftest answers from the recording).
 *
 * Time: T = iopk's IOP clock (36.864 MHz cycles). Advance(t) runs the IOP to t (the future vblank / EE-wait entry);
 * AdvanceEe(ee) = Advance(ee / 8 + base) as the hybrid's iop_advance (base = SetEeBase, the hybrid's ee_ahead_base). */
#pragma once
#include <stdint.h>

struct SndMixer;
namespace iopk { struct Hooks; struct ModuleArgs; }

namespace sndhost {

typedef int (*StartFn)(const iopk::ModuleArgs *m);                 /* = iopk::ModuleStartFn */
typedef void (*MixTap)(void *u, const int16_t *lr, int frames);    /* interleaved L / R s16 */
typedef void *(*EeServerFn)(unsigned int fno, void *buf, int size); /* sceSifRegisterRpc's func; returns the reply */

/* the game side of the native IOP */
struct Binding {
    /* power-on: iopk::Reset + mixer / sd / kernel hooks (ee = the host's EE-side hooks; the binding adds the spu_*
     * fields) + the native module interfaces */
    void (*init)(SndMixer *m, const iopk::Hooks *ee);
    /* native start of an IRX module name; *image_size set (nonzero) when the name is known (start 0 = known, no code:
     * a phantom image with an empty start); 0 with *image_size untouched = not a native module (not run) */
    StartFn (*resolve)(const char *name, uint32_t *image_size);
    void (*set_mix_tap)(MixTap fn, void *user);                     /* mixer output observer (0 = discard) */
    /* an IRX without a module name (its header has none; the hybrid traces it "(unnamed)"): the binding's name for an
     * image of this size, 0 = unknown (then the load path is the name: not run). Optional (0 = no unnamed modules) */
    const char *(*name_by_size)(uint32_t image_size);
};

struct Config {
    const Binding *bind;
    SndMixer *mixer;   /* 0 = the host creates its own (kept across Init) */
    /* IOP -> EE SIF DMA into EE memory (nonzero = done); 0 = no EE memory (the request is dropped by iopk) */
    int (*ee_write)(void *u, uint32_t ee_addr, const void *src, uint32_t size);
    void *ee_user;
};

void Init(const Config *c);      /* power-on (also a re-init: registry, binds and module count cleared) */
void Quit();                     /* hooks off, registry cleared (the own mixer is kept) */
int Ready();
SndMixer *Mixer();

/* IOP heap (sceSifAllocIopHeap / sceSifFreeIopHeap): IOP addresses, 0 = no memory */
uint32_t AllocIopHeap(uint32_t size);
int FreeIopHeap(uint32_t addr);
/* EE -> IOP SIF DMA (sceSifSetDma with an IOP destination); 0 = bad range */
int DmaToIop(const void *src, uint32_t iop_dst, uint32_t size);

/* modules: returns the module id (>= 1 started, 1000 + n not run) or -1 (bad image); *result = module_start's word */
int LoadModuleBuffer(uint32_t iop_addr, const char *args, int argl, int *result);   /* "iopmem:%06x" */
int LoadModuleFile(const char *path, const char *args, int argl, int *result);      /* "cdrom0:\..." from the disc */
/* the common start: name + image size known (fn 0 = the binding's start; an explicit fn overrides it, e.g. a
 * selftest stand-in); *base = the phantom image address */
int StartModule(const char *name, const char *path, uint32_t image_size, const char *args, int argl, StartFn fn,
                uint32_t *base, int *result);

/* EE -> IOP RPC. Bind = the hybrid's sceSifBindRpc wait (the first bind of a sid without a server runs the IOP up to
 * 0.5 s until it exists); 1 = served. RpcCall: 0 = served, -1 = no server (recv untouched) */
int Bind(uint32_t sid);
int RpcCall(uint32_t sid, uint32_t fno, const void *send, uint32_t ssize, void *recv, uint32_t rsize);

/* EE RPC servers (IOP -> EE). key = the caller's serve-data object (sceSifRegisterRpc's sd; 0 = by sid), queue = its
 * queue object (ServeQueue's filter). A sid registered again replaces the old entry. */
int RegisterEeServer(uint32_t sid, EeServerFn fn, void *buf, const void *queue, const void *key);
void RemoveEeServer(const void *key);
int ServeQueue(const void *queue);   /* serve the pending requests of this queue's servers; returns the count */
int ServePending();                  /* all registered (non-external) servers */
void SetAutoServe(int on);
void SetPostedHook(void (*fn)(void *u, uint32_t sid), void *user);   /* called on an IOP thread: mark / wake only */

/* time */
uint64_t Now();
void Advance(uint64_t iop_target);
void AdvanceEe(uint64_t ee_cycles);
void SetEeBase(uint64_t iop_cycles);

/* mixer output tap for the audio device (N9; port/platform/audio). Kept by the host: a tap set before Init is bound at
 * Init (and again after Quit + Init). Runs on the sim thread inside the IOP clock (Advance), mixer chunks of <= 256 */
void SetMixTap(MixTap fn, void *user);

}  // namespace sndhost
