/* synth - see synth.h. Function comments give the original's module offset.
 * Memory accesses into the image / blob go through IOP addresses (R8 / W32 ...: little-endian, an address outside IOP
 * RAM reads 0 and drops the write - the original would scribble over whatever lies there; only FlushSe's unchecked port
 * byte can get that far). */
#include "synth.h"
#include "fssd.h"
#include "../../platform/snd/iop/sd.h"
#include "../../platform/snd/iop/sein.h"
#include <stdio.h>
#include <string.h>

namespace synth {

/* ---------------------------------------------------------------- module state */

static uint32_t s_base;
static int32_t _fdata;                 /* 0x2b90: sema id of the surround lock (-1 until hsynInit) */
static int32_t s_iRpcRet;              /* 0x2ba0: the reply word (returned by pointer) */
static int32_t s_iIsInit;              /* 0x2ba4 */
static int32_t s_iIsStart;             /* 0x2ba8 */
/* 0xcc40: IntFunc's argument block + the two halfwords after it */
struct TickBlock { int32_t tid, timer, state; uint32_t clk; uint16_t mvol, stereo; };
static TickBlock s_tick;
static int32_t s_iSurroundCalledNum;   /* 0x10c80 */
static int32_t s_iDebugFlag;           /* 0x10c84 */
static Hsyn s_hsyn;
static Midi s_midi;
static uint32_t s_nTicks, s_nWakes;    /* test view */

/* image offsets of the buffers handed out as IOP addresses (the rest: synth.h) */
constexpr uint32_t OFS_SE_RETRIEVE = 0x10cc0;   /* s_ucSeRetrieveVoiceId u8[0x40] */
constexpr uint32_t OFS_SIINFO = 0x10d00;        /* s_siInfo 0x7f x 16 */
constexpr uint32_t OFS_SSCSTAT = 0x11500;       /* s_sscStat 16 x 0x48: sein ctx per SetCslSeStream idx */
constexpr uint32_t OFS_USEVOICE = 0x11980;      /* s_ucUseVoiceNoBySeId u8[0x40] */
constexpr uint32_t OFS_CHSTAT = 0x119c0;        /* s_hscsChStat 0x10 */
constexpr uint32_t OFS_MIDI_FLAG = 0x3514;      /* g_iemData + 0x964 + n: u8 per midi port */
constexpr uint32_t OFS_MIDI_LOOP = 0x3504;      /* g_iemData + 0x954 + 4n: s32 loop word */
constexpr uint32_t OFS_MIDI_TEMPO = 0x3518;     /* g_iemData + 0x968 + 2n: u16 relative tempo */
constexpr uint32_t OFS_CHMSGCB0 = 0x1ff4;       /* chMsgCB0; CB n at + 0x5c * n */

static uint32_t IEM() { return s_base + OFS_IEM; }
static uint32_t HCTX() { return s_base + OFS_HSYN_CTX; }
static uint32_t SEBUF(uint32_t port) { return IEM() + port * 0x408 + 0x6010; }   /* stream buffer header {cap, used} */

static uint8_t R8(uint32_t a) { const uint8_t *p = iopk::Ptr(a); return p ? *p : 0; }
static uint16_t R16(uint32_t a) { const uint8_t *p = iopk::Ptr(a); uint16_t v = 0; if (p) memcpy(&v, p, 2); return v; }
static uint32_t R32(uint32_t a) { const uint8_t *p = iopk::Ptr(a); uint32_t v = 0; if (p) memcpy(&v, p, 4); return v; }
static void W8(uint32_t a, uint8_t v) { uint8_t *p = iopk::Ptr(a); if (p) *p = v; }
static void W16(uint32_t a, uint16_t v) { uint8_t *p = iopk::Ptr(a); if (p) memcpy(p, &v, 2); }
static void W32(uint32_t a, uint32_t v) { uint8_t *p = iopk::Ptr(a); if (p) memcpy(p, &v, 4); }

/* ---------------------------------------------------------------- hsyn / modmidi (interfaces) */

static int sceHSyn_ATick(uint32_t c) { return s_hsyn.ATick ? s_hsyn.ATick(s_hsyn.user, c) : 0; }
static int sceHSyn_Load(uint32_t c, uint32_t port, uint32_t bd, uint32_t hd, uint32_t slot)
{
    return s_hsyn.Load ? s_hsyn.Load(s_hsyn.user, c, port, bd, hd, slot) : 0;
}
static int sceHSyn_VoiceTrans(int ch, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return s_hsyn.VoiceTrans ? s_hsyn.VoiceTrans(s_hsyn.user, ch, a1, a2, a3) : 0;
}
static int sceHSyn_SetEffectAttr(uint32_t rec) { return s_hsyn.SetEffectAttr ? s_hsyn.SetEffectAttr(s_hsyn.user, rec) : 0; }
static int sceHSyn_SetVolume(uint32_t c, uint32_t port, uint16_t v)
{
    return s_hsyn.SetVolume ? s_hsyn.SetVolume(s_hsyn.user, c, port, v) : 0;
}
static int sceHSyn_AllNoteOff(uint32_t c, uint32_t port) { return s_hsyn.AllNoteOff ? s_hsyn.AllNoteOff(s_hsyn.user, c, port) : 0; }
static int sceHSyn_ResetAllControler(uint32_t c, uint32_t port)
{
    return s_hsyn.ResetAllControler ? s_hsyn.ResetAllControler(s_hsyn.user, c, port) : 0;
}
static int sceHSyn_SetVoiceStatBuffer(uint32_t b) { return s_hsyn.SetVoiceStatBuffer ? s_hsyn.SetVoiceStatBuffer(s_hsyn.user, b) : 0; }
static int sceHSyn_GetChStat(uint32_t c, uint32_t port, uint32_t b)
{
    return s_hsyn.GetChStat ? s_hsyn.GetChStat(s_hsyn.user, c, port, b) : 0;
}
static int sceHSyn_SESetMaxVoices(int n) { return s_hsyn.SESetMaxVoices ? s_hsyn.SESetMaxVoices(s_hsyn.user, n) : 0; }
static int sceHSyn_SetOutputMode(int m) { return s_hsyn.SetOutputMode ? s_hsyn.SetOutputMode(s_hsyn.user, m) : 0; }
static int sceHSyn_SERetrieveVoiceNumberByID(uint32_t c, uint32_t port, uint32_t id, uint32_t b, int n)
{
    return s_hsyn.SERetrieveVoiceNumberByID ? s_hsyn.SERetrieveVoiceNumberByID(s_hsyn.user, c, port, id, b, n) : 0;
}
static int sceHSyn_MSGetVoiceStateByID(uint32_t c, uint32_t a1, uint32_t a2, uint32_t b, int n)
{
    return s_hsyn.MSGetVoiceStateByID ? s_hsyn.MSGetVoiceStateByID(s_hsyn.user, c, a1, a2, b, n) : 0;
}

static int sceMidi_Init(uint32_t c, uint32_t us) { return s_midi.Init ? s_midi.Init(s_midi.user, c, us) : 0; }
static int sceMidi_ATick(uint32_t c) { return s_midi.ATick ? s_midi.ATick(s_midi.user, c) : 0; }
static int sceMidi_Load(uint32_t c, uint32_t port) { return s_midi.Load ? s_midi.Load(s_midi.user, c, port) : 0; }
static int sceMidi_SelectMidi(uint32_t c, uint32_t port, uint32_t n)
{
    return s_midi.SelectMidi ? s_midi.SelectMidi(s_midi.user, c, port, n) : 0;
}
static int sceMidi_MidiPlaySwitch(uint32_t c, uint32_t port, uint32_t on)
{
    return s_midi.PlaySwitch ? s_midi.PlaySwitch(s_midi.user, c, port, on) : 0;
}
static int sceMidi_MidiSetLocation(uint32_t c, uint32_t port, uint32_t loc)
{
    return s_midi.SetLocation ? s_midi.SetLocation(s_midi.user, c, port, loc) : 0;
}
static int sceMidi_MidiGetAbsoluteTempo(uint32_t c, uint32_t port)
{
    return s_midi.GetAbsoluteTempo ? s_midi.GetAbsoluteTempo(s_midi.user, c, port) : 0;
}
static int sceMidi_MidiSetRelativeTempo(uint32_t c, uint32_t port, uint16_t t)
{
    return s_midi.SetRelativeTempo ? s_midi.SetRelativeTempo(s_midi.user, c, port, t) : 0;
}

void SetHsyn(const Hsyn *h)
{
    if (h) s_hsyn = *h;
    else memset(&s_hsyn, 0, sizeof s_hsyn);
}
void SetMidi(const Midi *m)
{
    if (m) s_midi = *m;
    else memset(&s_midi, 0, sizeof s_midi);
}
uint32_t Base() { return s_base; }

void GetView(View *v)
{
    v->tick_tid = s_tick.tid;
    v->timer_id = s_tick.timer;
    v->state = s_tick.state;
    v->clk = s_tick.clk;
    v->ticks = s_nTicks;
    v->wakes = s_nWakes;
    v->is_init = s_iIsInit;
    v->is_start = s_iIsStart;
}

/* ---------------------------------------------------------------- forward (ladder targets) */

static int synthInit();
static int synthQuit();
static void synthSystemStart();
static void synthSystemStop();
static void synthSetMasterVolume(uint16_t vol);
static int synthVoiceTransStatus(int16_t ch, int16_t flag);
static int synthVoiceTrans(int16_t ch, uint16_t mode, uint32_t iop, uint32_t spu, uint32_t size);
static uint32_t synthGetIopPacketAddress();
static void synthCopyFromPacket(uint32_t dst, uint32_t size);
static void synthSwitchStereo(uint16_t v);
static int synthSetReberb(uint32_t rec);
static int FlushSe();
static int MakeMonoHd(uint32_t dst, uint32_t src, uint32_t size);
static int GetHdAdsr(uint32_t hd, uint32_t unused, uint32_t prog);
static void SetHdAdsr(uint32_t hd, uint32_t unused, uint32_t prog, uint32_t adsr);
static int SetCslSeStream(uint32_t port, uint32_t idx);
static int GeEeVolumePack(uint32_t buf);
static void AtickSetSurroundVolume();
static int hsynInit();
static void hsynAtick();
static int midiInit();
static void midiSystemStart();
static void midiSystemStop();
static void midiAtick();
static int midiLoad(uint32_t port, uint32_t sq);
static int midiPlay(uint32_t port, uint32_t loop);
static int midiStop(uint32_t port);
static int midiPause(uint32_t port);
static int midiReplay(uint32_t port);
static int midiIsPlay(uint32_t port);
static int midiSwitchStereo(uint32_t port, uint32_t bd, uint32_t hd, uint32_t slot, uint32_t mono);
static int midiRelTempo(uint32_t port, uint16_t t);

/* ---------------------------------------------------------------- RPC ladder */

static uint32_t rw(const uint8_t *b, int o) { uint32_t v; memcpy(&v, b + o, 4); return v; }
static uint16_t rhu(const uint8_t *b, int o) { uint16_t v; memcpy(&v, b + o, 2); return v; }
static int16_t rh(const uint8_t *b, int o) { return (int16_t)rhu(b, o); }

/* 0x29c: the original is a linear if-chain on cmd & 0xffff00 (low byte ignored); s_iRpcRet is cleared and *outsize = 4
 * first; the handlers that hand back a buffer (GetVoiceStat / GetChStat / GetSeStatTable / GetSeStrStatTable) set
 * *outsize themselves. Unknown commands leave -1. */
static void *RpcCallBack(uint32_t cmd, void *buf, int, int *outsize)
{
    s_iRpcRet = 0;
    *outsize = 4;
    const uint8_t *b = (const uint8_t *)buf;
    void *r = &s_iRpcRet;
    switch (cmd & 0xffff00u) {
    case 0xff00: s_iDebugFlag = (int32_t)rw(b, 0); break;                        /* hsynSetDebugFlag 0x1fe8 */
    case 0x400700: s_iRpcRet = synthSetReberb(iopk::Addr(buf)); break;
    case 0x20800: s_iRpcRet = midiRelTempo(rw(b, 0), rhu(b, 4)); break;
    case 0x20700: s_iRpcRet = midiSwitchStereo(rw(b, 0), rw(b, 4), rw(b, 8), rw(b, 0xc), rw(b, 0x10)); break;
    case 0x20100: s_iRpcRet = midiIsPlay(rw(b, 0)); break;
    case 0x20600: s_iRpcRet = midiReplay(rw(b, 0)); break;
    case 0x20500: s_iRpcRet = midiPause(rw(b, 0)); break;
    case 0x20400: s_iRpcRet = midiStop(rw(b, 0)); break;
    case 0x20300: s_iRpcRet = midiPlay(rw(b, 0), rw(b, 4)); break;
    case 0x20000: s_iRpcRet = midiLoad(rw(b, 0), rw(b, 4)); break;
    case 0x11d00: s_iRpcRet = (int32_t)(s_base + OFS_SESTREAM); break;           /* hsynGetSeStreamBuffer 0x1fdc */
    case 0x411c00: s_iRpcRet = GeEeVolumePack(iopk::Addr(buf)); break;          /* hsynSetSurroundVolumePack */
    case 0x11b00: {                                                               /* hsynSetSurroundVolume 0x1f60 */
        if (s_iSurroundCalledNum < 0x7f) {
            uint32_t e = s_base + OFS_SIINFO + (uint32_t)s_iSurroundCalledNum * 16;
            W32(e, rw(b, 4) + 0x10000000u);
            W32(e + 4, rw(b, 8));
            W32(e + 8, rw(b, 0xc));
            s_iSurroundCalledNum++;
        }
        break;
    }
    case 0x811800: *outsize = 0x40; r = iopk::Ptr(s_base + OFS_SE_RETRIEVE); break;   /* hsynGetSeStrStatTable 0x1f4c */
    case 0x11700:                                                                  /* hsynGetSeStrStat 0x1f0c */
        s_iRpcRet = sceHSyn_SERetrieveVoiceNumberByID(HCTX(), rw(b, 0), rw(b, 4) + 0x10000000u, s_base + OFS_SE_RETRIEVE, 0x30);
        break;
    case 0x11100: W8(s_base + 0x363e + rw(b, 0) * 0x558, 0); break;              /* hsynSetCslMidiStream 0x1ee0 */
    case 0x11600: SetCslSeStream(rw(b, 0), rw(b, 4)); break;
    case 0x11500: s_iRpcRet = sd::GetParam((uint16_t)((rw(b, 0) | rw(b, 4) << 1) | 0x100)); break;   /* GetSeVoiceRight 0x1eac */
    case 0x11400: s_iRpcRet = sd::GetParam((uint16_t)(rw(b, 0) | rw(b, 4) << 1)); break;             /* GetSeVoiceLeft 0x1e84 */
    case 0x11300: sd::SetParam((uint16_t)((rw(b, 0) | rw(b, 4) << 1) | 0x100), rhu(b, 8)); break;    /* SetSeVoiceRight 0x1e6c */
    case 0x11200: sd::SetParam((uint16_t)(rw(b, 0) | rw(b, 4) << 1), rhu(b, 8)); break;              /* SetSeVoiceLeft 0x1e58 */
    case 0x811100: *outsize = 0x40; r = iopk::Ptr(s_base + OFS_USEVOICE); break;  /* hsynGetSeStatTable 0x1e44 */
    case 0x11000:                                                                  /* hsynHSynMsGetVoiceStateById 0x1e08 */
        s_iRpcRet = sceHSyn_MSGetVoiceStateByID(HCTX(), rw(b, 0), b[4], s_base + OFS_USEVOICE, 0x30);
        break;
    case 0x1300: {                                                                 /* hsynGetHdVolume 0x1db8 */
        uint32_t hd = rw(b, 0), prog = rw(b, 4);
        if (!hd) s_iRpcRet = 0x7f;
        else {
            uint32_t chunk = hd + R32(hd + 0x24);
            uint32_t off = R32(prog * 4 + chunk + 0x10);
            s_iRpcRet = off == 0xffffffffu ? 0x7f : R8(chunk + off + 6);
        }
        break;
    }
    case 0x1200: SetHdAdsr(rw(b, 0), rw(b, 4), rw(b, 8), rw(b, 0xc)); break;
    case 0x1100: s_iRpcRet = GetHdAdsr(rw(b, 0), rw(b, 4), rw(b, 8)); break;
    case 0x10900:                                                                  /* hsynHSynGetChStat 0x1d60 */
        sceHSyn_GetChStat(HCTX(), rw(b, 0), s_base + OFS_CHSTAT);
        *outsize = 0x10;
        r = iopk::Ptr(s_base + OFS_CHSTAT);
        break;
    case 0x10800: s_iRpcRet = FlushSe(); break;                                   /* hsynSeFlush 0x1d58 */
    case 0x10700: s_iRpcRet = MakeMonoHd(rw(b, 0), rw(b, 4), rw(b, 8)); break;    /* hsynHSynMakeMonoHd 0x1d50 */
    case 0x10600: {                                                                /* hsynHSynSetVolume 0x1d28 */
        uint16_t v = rhu(b, 4);
        if (v) v = (uint16_t)(v << 1 | 0xf);
        s_iRpcRet = sceHSyn_SetVolume(HCTX(), rw(b, 0), v);
        break;
    }
    case 0x10500: W8(s_base + 0x363d + rw(b, 0) * 0x558, b[4]); break;           /* hsynHSynSetVoiceNum 0x1d00 */
    case 0x10400: W8(s_base + 0x363c + rw(b, 0) * 0x558, b[4]); break;           /* hsynHSynSetPriority 0x1cd8 */
    case 0x10300: s_iRpcRet = sceHSyn_AllNoteOff(HCTX(), rw(b, 0)); break;        /* hsynHSynAllNoteOff 0x1cc8 */
    case 0x810200: *outsize = VOICESTAT_SIZE; r = iopk::Ptr(s_base + OFS_VOICESTAT); break;   /* hsynHSynGetVoiceStat 0x1cb4 */
    case 0x10100: s_iRpcRet = sceHSyn_Load(HCTX(), rw(b, 0), rw(b, 4), rw(b, 8), rw(b, 0xc)); break;   /* hsynHSynLoad 0x1c7c */
    case 0x10000: s_iRpcRet = sceHSyn_VoiceTrans(rh(b, 0), rw(b, 4), rw(b, 8), rw(b, 0xc)); break;     /* hsynHSynVoiceTrans */
    case 0x1000: sd::SetCoreAttr(rhu(b, 0), rhu(b, 4)); break;                   /* synthSdSetCoreAttr 0x1098 */
    case 0xf00: s_iRpcRet = (int32_t)sd::BlockTransStatus(rh(b, 0)); break;      /* synthSdBlockTransStatus 0x1090 (flag lh +4 unused by sd) */
    /* synthSdBlockTrans 0x1088: a `j` with RpcCallBack's frame, so the 5th argument is whatever its sp + 0x10 slot
     * holds (unreached; passed as 0) */
    case 0xe00: s_iRpcRet = sd::BlockTrans(rh(b, 0), rhu(b, 4), iopk::Ptr(rw(b, 8)), rw(b, 0xc), 0); break;
    case 0xd00: sd::SetParam(rhu(b, 0), rhu(b, 4)); break;                       /* synthSdSetParam 0x1080 */
    case 0xc00: synthSwitchStereo(rhu(b, 0)); break;
    case 0xb00: synthCopyFromPacket(rw(b, 0), rw(b, 4)); break;
    case 0xa00: s_iRpcRet = (int32_t)synthGetIopPacketAddress(); break;
    case 0x900: s_iRpcRet = synthVoiceTrans(rh(b, 0), rhu(b, 4), rw(b, 8), rw(b, 0xc), rw(b, 0x10)); break;
    case 0x800: s_iRpcRet = synthVoiceTransStatus(rh(b, 0), rh(b, 4)); break;
    case 0x300: synthSetMasterVolume(rhu(b, 0)); break;
    case 0x500: synthSystemStop(); break;
    case 0x400: synthSystemStart(); break;
    case 0x200: s_iRpcRet = synthQuit(); break;
    case 0x100: s_iRpcRet = synthInit(); break;
    default: s_iRpcRet = -1; break;
    }
    return r;
}

/* ---------------------------------------------------------------- module start, timer, thread */

/* 0xa54 (+ the zeroed image's statics; _fdata's .data value -1) */
int ModuleStart(const iopk::ModuleArgs *m)
{
    s_base = m->base;
    _fdata = -1;
    s_iRpcRet = s_iIsInit = s_iIsStart = 0;
    memset(&s_tick, 0, sizeof s_tick);
    s_iSurroundCalledNum = s_iDebugFlag = 0;
    s_nTicks = s_nWakes = 0;
    fssd::Register(MODULE, RpcCallBack);
    return 0;
}

/* 0xa7c: the hard-timer handler; its return value is the next compare (iopk re-arms with it): the period, 153,612 */
static unsigned int IntFunc(void *arg)
{
    TickBlock *t = (TickBlock *)arg;
    if (t->state == 1) {
        s_nWakes++;
        iopk::iWakeupThread(t->tid);
    }
    return t->clk;
}

/* 0xabc */
static void ATick(void *)
{
    for (;;) {
        iopk::SleepThread();
        if (s_tick.state == 2) iopk::ExitThread();
        int tmp;
        iopk::DisableIntr(0xf, &tmp);
        s_tick.state = 0;
        s_nTicks++;
        hsynAtick();
        midiAtick();
        s_tick.state = 1;
        iopk::EnableIntr(0xf);
    }
}

/* 0xb24: SPU init (order exact; the BVOL zero pair 0xf80 / 0x1080 per core is the "second core-1 BVOL pair") */
static void Init()
{
    sd::SetAddr(0x1d00, 0x1dffff);
    sd::SetAddr(0x1d01, 0x1fffff);
    sd::SetParam(0x800, 0xfc0);
    sd::SetParam(0x801, 0xfcc);
    for (uint32_t c = 0; c < 2; c++) {
        sd::SetParam((uint16_t)(c | 0xb80), 0);
        sd::SetParam((uint16_t)(c | 0xc80), 0);
        sd::SetParam((uint16_t)(c | 0xf80), 0);
        sd::SetParam((uint16_t)(c | 0x1080), 0);
    }
    sd::ClearEffectWorkArea(1, 0, 7);
    s_tick.mvol = 0x3fff;
    s_tick.stereo = 1;
}

/* 0xbf4: the timer runs before the thread exists (IntFunc ignores ticks while state != 1); StartThread preempts (prio
 * 0x17 < the RPC thread's 0x19), ATick runs to its first SleepThread */
static int InitTimer()
{
    iopk::SysClock c;
    iopk::USec2SysClock(0x1047, &c);
    s_tick.clk = c.lo;
    s_tick.timer = iopk::AllocHardTimer(1, 0x20, 1);
    if (s_tick.timer <= 0) return -1;
    if (iopk::SetTimerHandler(s_tick.timer, s_tick.clk, IntFunc, &s_tick)) return -1;
    if (iopk::SetupHardTimer(s_tick.timer, 1, 0, 1)) return -1;
    if (iopk::StartHardTimer(s_tick.timer)) return -1;
    iopk::ThreadParam p;
    p.attr = 0x02000000;
    p.option = 0;
    p.entry = ATick;
    p.stack_size = 0x800;
    p.priority = 0x17;
    s_tick.tid = iopk::CreateThread(&p);
    if (s_tick.tid <= 0) return -1;
    iopk::StartThread(s_tick.tid, 0);
    s_tick.state = 0;
    return 1;
}

/* 0xd1c: the init calls' results are ignored */
static int synthInit()
{
    if (s_iIsInit == 1) return 0;
    Init();
    midiInit();
    hsynInit();
    InitTimer();
    s_iIsInit = 1;
    return 1;
}

/* 0xd7c (NOT RUN): WakeupThread preempts; ATick sees state 2 and exits */
static int synthQuit()
{
    int tmp;
    iopk::DisableIntr(0xf, &tmp);
    s_tick.state = 2;
    iopk::WakeupThread(s_tick.tid);
    iopk::FreeHardTimer(s_tick.timer);
    iopk::DeleteThread(s_tick.tid);
    /* midiQuit 0x234c / hsynQuit 0x1c30: return 1, nothing else */
    s_iIsInit = 0;
    return 1;
}

/* 0xdf4: WakeupThread preempts (ATick runs one pass right here) */
static void synthSystemStart()
{
    if (s_iIsStart == 1) return;
    /* hsynSystemStart 0x1c38: empty */
    midiSystemStart();
    if (s_tick.state == 0) {
        s_tick.state = 1;
        iopk::WakeupThread(s_tick.tid);
        iopk::EnableIntr(0xf);
    }
    sd::SetParam(0x980, 0x3fff);
    sd::SetParam(0xa80, 0x3fff);
    sd::SetParam(0x981, s_tick.mvol);
    sd::SetParam(0xa81, s_tick.mvol);
    s_iIsStart = 1;
}

/* 0xeac: 0xffff (not 0) to the four master volumes */
static void synthSystemStop()
{
    if (!s_iIsStart) return;
    sd::SetParam(0x980, 0xffff);
    sd::SetParam(0xa80, 0xffff);
    sd::SetParam(0x981, 0xffff);
    sd::SetParam(0xa81, 0xffff);
    /* hsynSystemStop 0x1c40: empty */
    midiSystemStop();
    s_iIsStart = 0;
}

/* 0xf20: no s_iIsStart test */
static void synthSetMasterVolume(uint16_t vol)
{
    s_tick.mvol = vol;
    sd::SetParam(0x980, 0x3fff);
    sd::SetParam(0xa80, 0x3fff);
    sd::SetParam(0x981, s_tick.mvol);
    sd::SetParam(0xa81, s_tick.mvol);
}

/* 0xf78: restores a hard-coded 0x19, not the previous priority */
static int synthVoiceTransStatus(int16_t ch, int16_t flag)
{
    iopk::ChangeThreadPriority(0, 0x56);
    int r = sd::VoiceTransStatus(ch, flag);
    iopk::ChangeThreadPriority(0, 0x19);
    return r;
}

/* 0xfd0 */
static int synthVoiceTrans(int16_t ch, uint16_t mode, uint32_t iop, uint32_t spu, uint32_t size)
{
    return sd::VoiceTrans(ch, mode, iopk::Ptr(iop), spu, size);
}

/* 0xff4 */
static uint32_t synthGetIopPacketAddress() { return s_base + OFS_PACKET; }

/* 0x1000: no bound against the 0x4000 packet buffer */
static void synthCopyFromPacket(uint32_t dst, uint32_t size)
{
    uint32_t s = s_base + OFS_PACKET;
    while (size--) W8(dst++, R8(s++));
}

/* 0x1034 */
static void synthSwitchStereo(uint16_t v)
{
    s_tick.stereo = v;
    sceHSyn_SetOutputMode(s_tick.stereo == 1 ? 1 : 0);
}

/* 0x10a0: core-1 master volume sweep down, wait (guard-1 poll) until both MVOLX read 0, set the effect, restore R then L */
static int synthSetReberb(uint32_t rec)
{
    sd::SetParam(0x981, 0xa001);
    sd::SetParam(0xa81, 0xa001);
    for (;;) {
        uint32_t a = sd::GetParam(0x1181) & 0xffff;
        uint32_t b = sd::GetParam(0x1281) & 0xffff;
        if ((int32_t)(a | b) <= 0) break;
        iopk::WaitNextEvent();
    }
    int r = sceHSyn_SetEffectAttr(rec);
    sd::SetParam(0xa81, s_tick.mvol);
    sd::SetParam(0x981, s_tick.mvol);
    return r;
}

/* 0x1134: the SE byte stream (ff <port> <len> <payload>) appended per port to the stream buffers. Quirks kept: the port
 * byte is unchecked; the capacity test compares the 0x408 cap with used + len (the 8-byte header ignored); a non-0xff
 * record byte or a full buffer drops the rest of the whole flush (other ports too). A tick landing inside is dropped in
 * the original (state 0); natively nothing runs inside, so none is. */
static int FlushSe()
{
    int tmp;
    iopk::DisableIntr(0xf, &tmp);
    s_tick.state = 0;
    uint32_t sb = s_base + OFS_SESTREAM;
    uint32_t n = R32(sb);
    if (n) {
        uint32_t end = sb + n + 4;
        uint32_t p = sb + 4;
        while (p < end) {
            if (R8(p++) != 0xff) { p = end; continue; }
            uint32_t port = R8(p), len = R8(p + 1);
            uint32_t hdr = SEBUF(port);
            uint32_t dst = hdr + R32(hdr + 4) + 8;
            uint32_t used = R32(hdr + 4) + len;
            p += 2;
            if (R32(hdr) < used) { p = end; continue; }
            W32(hdr + 4, used);
            for (int32_t k = (int32_t)len; k; k--) W8(dst++, R8(p++));
        }
        W32(sb, 0);
    }
    s_tick.state = 1;
    iopk::EnableIntr(0xf);
    return 0;
}

/* 0x1260: copy an HD image, then centre every pan (Prog entry +7, tone +0x11, Smpl +0xd = 0x3f; Prog +0xa = 0). Index
 * bounds are `<=` the chunk's stored last index; Sset / Smpl offsets are not tested for -1 (only Prog's are) */
static int MakeMonoHd(uint32_t dst, uint32_t src, uint32_t size)
{
    uint32_t d = dst, s = src;
    for (uint32_t k = size; k; k--) W8(d++, R8(s++));
    uint32_t prog = dst + R32(dst + 0x24), sset = dst + R32(dst + 0x28), smpl = dst + R32(dst + 0x2c);
    for (uint32_t i = 0; !(R32(prog + 0xc) < i); i++) {
        uint32_t off = R32(prog + i * 4 + 0x10);
        if (off == 0xffffffffu) continue;
        uint32_t e = prog + off;
        if (!R8(e + 4)) continue;
        W8(e + 7, 0x3f);
        W8(e + 0xa, 0);
        uint32_t tone = e + R32(e);
        for (uint32_t t = R8(e + 4); t; t--, tone += 0x14) {
            W8(tone + 0x11, 0x3f);
            uint32_t ssi = R16(tone);
            if (R32(sset + 0xc) < ssi) continue;
            uint32_t se = sset + R32(sset + ssi * 4 + 0x10);
            for (int32_t k = 0; k < (int32_t)R8(se + 3); k++) {
                uint32_t si = R16(se + (uint32_t)k * 2 + 4);
                if (R32(smpl + 0xc) < si) continue;
                W8(smpl + R32(smpl + si * 4 + 0x10) + 0xd, 0x3f);
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- hsyn front (S2 part) */

/* 0x13a8 (NOT RUN): HD walk Prog[prog] -> its tones -> Sset -> Smpl, returns (adsr1 << 16) | adsr2 at Smpl + 0x12 / 0x14
 * of the LAST sample reached (0 if none). The second RPC word is not used (a1 is overwritten before any use); the prog
 * index is the third and is not bounds-checked (only its offset is tested for -1). */
static int GetHdAdsr(uint32_t hd, uint32_t, uint32_t prog)
{
    uint32_t pc = hd + R32(hd + 0x24), sset = hd + R32(hd + 0x28), smpl = hd + R32(hd + 0x2c);
    uint32_t r = 0;
    uint32_t off = R32(pc + prog * 4 + 0x10);
    if (off == 0xffffffffu) return 0;
    uint32_t e = pc + off;
    uint32_t n = R8(e + 4);
    if (!n) return 0;
    for (uint32_t tone = e + R32(e); n; n--, tone += 0x14) {
        uint32_t ssi = R16(tone);
        if (R32(sset + 0xc) < ssi) continue;
        uint32_t se = sset + R32(sset + ssi * 4 + 0x10);
        for (int32_t k = 0; k < (int32_t)R8(se + 3); k++) {
            uint32_t si = R16(se + (uint32_t)k * 2 + 4);
            if (R32(smpl + 0xc) < si) continue;
            uint32_t s = smpl + R32(smpl + si * 4 + 0x10);
            r = (uint32_t)R16(s + 0x12) << 16 | R16(s + 0x14);
        }
    }
    return (int)r;
}

/* 0x14a0 (NOT RUN): the write twin of GetHdAdsr: every sample reached gets adsr >> 16 / adsr & 0xffff */
static void SetHdAdsr(uint32_t hd, uint32_t, uint32_t prog, uint32_t adsr)
{
    uint32_t pc = hd + R32(hd + 0x24), sset = hd + R32(hd + 0x28), smpl = hd + R32(hd + 0x2c);
    uint32_t off = R32(pc + prog * 4 + 0x10);
    if (off == 0xffffffffu) return;
    uint32_t e = pc + off;
    uint32_t n = R8(e + 4);
    if (!n) return;
    for (uint32_t tone = e + R32(e); n; n--, tone += 0x14) {
        uint32_t ssi = R16(tone);
        if (R32(sset + 0xc) < ssi) continue;
        uint32_t se = sset + R32(sset + ssi * 4 + 0x10);
        for (int32_t k = 0; k < (int32_t)R8(se + 3); k++) {
            uint32_t si = R16(se + (uint32_t)k * 2 + 4);
            if (R32(smpl + 0xc) < si) continue;
            uint32_t s = smpl + R32(smpl + si * 4 + 0x10);
            W16(s + 0x12, (uint16_t)(adsr >> 16));
            W16(s + 0x14, (uint16_t)adsr);
        }
    }
}

/* 0x159c (NOT RUN: RPC 0x11600 is never sent in Versus): switch hsyn port `port` to the fe-flavour SE stream fed by
 * modsein: the hsyn port entry (ctx + 0x1c + 16 port: {0, stream buffer, 0, port block + 0x11c}), the port block's
 * stream state (+0x11c .. +0x12c, +0x11e = 1: hsyn's "fe parser" bit), then sein ctx s_sscStat[idx] = {2, groups, ..}
 * with group 1 = {1, its entry {0, stream buffer}} and sceSEIn_Init on it; on success sceHSyn_SESetMaxVoices(0).
 * Quirk kept: ctx[4] (the group pointer) is s_sscStat[0] + 0x14 for EVERY idx (the asm adds idx * 0x48 to every other
 * field but not to this value), so idx > 0 validates ctx 0's group 1 (fails while ctx 0 was never set up). port / idx
 * are unchecked. The ladder drops the result (no s_iRpcRet store). */
static int SetCslSeStream(uint32_t port, uint32_t idx)
{
    uint32_t h = HCTX();
    uint32_t e = h + port * 16;
    W32(e + 0x1c, 0);
    W32(e + 0x20, SEBUF(port));
    W32(e + 0x24, 0);
    uint32_t blk = h + port * 0x558;
    W32(e + 0x28, blk + 0x11c);
    W8(blk + 0x11c, 0);
    W32(blk + 0x120, 0);
    W32(blk + 0x124, 0);
    W32(blk + 0x128, 0);
    W32(blk + 0x12c, 0);
    W8(blk + 0x11e, 1);
    W8(blk + 0x11f, 0);
    uint32_t s0 = s_base + OFS_SSCSTAT, ssc = s0 + idx * 0x48;
    W32(ssc + 0x10, 0);
    W32(ssc + 0xc, 0);
    W32(ssc, 2);
    W32(ssc + 4, s0 + 0x14);
    W32(ssc + 0x14, 0);
    W32(ssc + 0x18, 0);
    W32(ssc + 0x1c, 1);
    W32(ssc + 0x20, ssc + 0x24);
    W32(ssc + 0x24, 0);
    W32(ssc + 0x28, SEBUF(port));
    if (sein::Init(ssc) != 0) return -1;
    sceHSyn_SESetMaxVoices(0);
    return 0;
}

/* 0x16f8 (NOT RUN): the EE's surround pack (16 records of 12 bytes: s16 left, s16 right, s16 prog, u16 key, u32 hd)
 * -> s_siInfo[count++] = {key + 0x10000000, left', right'} under the _fdata sema. x' = (x & 0x3fff) * vol / 0x7f, or
 * with bit 14 set 0x7fff - (0x7fff - x) * vol / 0x7f (x sign-extended, signed div); vol = the HD Prog entry's byte +6.
 * Quirks kept: a record with hd == 0 or an absent prog (offset -1) still takes a slot (count++, entry not written);
 * the prog index is not bounds-checked; the loop stops when the count reaches 0x7f. */
static int GeEeVolumePack(uint32_t buf)
{
    iopk::WaitSema(_fdata);
    uint32_t s = buf;
    for (int32_t i = 0; i < 16; i++, s += 0xc, s_iSurroundCalledNum++) {
        int32_t n = s_iSurroundCalledNum;
        if (!(n < 0x7f)) break;
        uint32_t hd = R32(s + 8);
        if (!hd) continue;
        int32_t prog = (int16_t)R16(s + 4);
        uint32_t pc = hd + R32(hd + 0x24);
        uint32_t off = R32(pc + (uint32_t)prog * 4 + 0x10);
        if (off == 0xffffffffu) continue;
        int32_t vol = R8(pc + off + 6);
        uint32_t e = s_base + OFS_SIINFO + (uint32_t)n * 16;
        W32(e, R16(s + 6) + 0x10000000u);
        for (uint32_t k = 0; k < 2; k++) {
            int32_t x = (int16_t)R16(s + k * 2);
            uint32_t o = e + 4 + k * 4;
            if ((x & 0x4000) > 0) {
                W32(o, (uint32_t)((0x7fff - x) * vol / 0x7f));
                W32(o, 0x7fff - R32(o));
            } else {
                W32(o, (uint32_t)((x & 0x3fff) * vol / 0x7f));
            }
        }
    }
    iopk::SignalSema(_fdata);
    return 0;
}

/* 0x191c: the queued surround entries (none in Versus: the loop body is unreached), then the count is cleared */
static void AtickSetSurroundVolume()
{
    iopk::WaitSema(_fdata);
    for (int32_t i = 0; i < s_iSurroundCalledNum; i++) {
        uint32_t e = s_base + OFS_SIINFO + (uint32_t)i * 16;
        for (uint32_t k = 0; k < 16; k++) {
            int n = sceHSyn_SERetrieveVoiceNumberByID(HCTX(), k, R32(e), s_base + OFS_SE_RETRIEVE, 0x30);
            for (int j = 0; j < n; j++) {
                uint32_t id = R8(s_base + OFS_SE_RETRIEVE + (uint32_t)j);
                uint32_t reg = (id / 24) | (id % 24) << 1;
                sd::SetParam((uint16_t)reg, R16(e + 4));
                sd::SetParam((uint16_t)(reg | 0x100), R16(e + 8));
            }
        }
    }
    s_iSurroundCalledNum = 0;
    iopk::SignalSema(_fdata);
}

/* 0x1a90: the hsyn ctx (written before sceHSyn_Init, which reads it), then the per-port defaults, the voice-stat
 * buffer, the 16 stream buffer headers and the surround sema */
static int hsynInit()
{
    uint32_t ctx = HCTX();
    W32(ctx + 0x10, 0);
    W32(ctx + 0xc, 0);
    W32(ctx, 1);
    W32(ctx + 4, ctx + 0x14);
    W32(ctx + 0x14, 0x20);
    W32(ctx + 0x18, ctx + 0x1c);
    for (int k = 15; k >= 0; k--) {
        uint32_t e = ctx + (uint32_t)k * 16, blk = ctx + (uint32_t)k * 0x558 + 0x11c;
        W32(e + 0x1c, 0);
        W32(e + 0x20, SEBUF((uint32_t)k));
        W32(e + 0x24, 0);
        W32(e + 0x28, blk);
        W8(blk, 0);
        W32(blk + 4, 0);
        W32(blk + 8, 0);
        W32(blk + 0xc, 0);
        W32(blk + 0x10, 0);
    }
    if (fssd::HsynInit(ctx, 0x1047)) return -1;
    for (int p = 15; p >= 0; p--) {
        sceHSyn_SetVolume(ctx, (uint32_t)p, 0x20f);
        sceHSyn_ResetAllControler(ctx, (uint32_t)p);
    }
    sceHSyn_SetVoiceStatBuffer(s_base + OFS_VOICESTAT);
    for (int p = 15; p >= 0; p--) {
        W32(SEBUF((uint32_t)p), 0x408);
        W32(SEBUF((uint32_t)p) + 4, 0);
    }
    iopk::SemaParam sp;
    sp.attr = 1;
    sp.option = 0;
    sp.init_count = 1;
    sp.max_count = 1;
    _fdata = iopk::CreateSema(&sp);
    if (_fdata < 0) return -1;
    return 1;
}

/* 0x1c48 */
static void hsynAtick()
{
    sceHSyn_ATick(HCTX());
    AtickSetSurroundVolume();
}

/* ---------------------------------------------------------------- modmidi front */

/* 0x1ff4 / 0x2050 / 0x20ac / 0x2108: mono (stereo flag 0) or the port's mono bit -> pan CC (0xab0 status class) value
 * forced to 0x3f */
uint32_t ChMsgCB(int n, uint32_t msg)
{
    if (s_tick.stereo == 0 || (R8(s_base + OFS_MIDI_FLAG + (uint32_t)n) & 0x10)) {
        if ((msg & 0xfff0) == 0xab0) msg = (msg & 0xffff) + 0x3f0000;
        return msg;
    }
    return msg;
}

/* 0x2164: the modmidi ctx (2 groups: input 8 entries = {SQ data, port struct} pairs, output 4 = the stream buffers),
 * the per-port words (loop -1, flag 0, tempo 0x100), sceMidi_Init, then the 4 port structs' masks / callback / buffers */
static int midiInit()
{
    uint32_t c = IEM();
    W32(c + 0x10, 0);
    W32(c + 0xc, 0);
    W32(c, 2);
    W32(c + 4, c + 0x14);
    W32(c + 0x14, 8);
    W32(c + 0x18, c + 0x24);
    W32(c + 0x1c, 4);
    W32(c + 0x20, c + 0x64);
    for (uint32_t n = 0; n < 4; n++) {
        uint32_t e = c + n * 16;
        W32(e + 0x24, 0);
        W32(e + 0x28, 0);
        W32(e + 0x2c, 0);
        W32(e + 0x30, c + n * 0x234 + 0x84);
        W32(c + n * 8 + 0x64, 0);
        W32(c + n * 8 + 0x68, SEBUF(n));
        W32(c + n * 4 + 0x954, 0xffffffffu);
        W8(c + n + 0x964, 0);
        W16(c + n * 2 + 0x968, 0x100);
    }
    if (sceMidi_Init(c, 0x1047)) return -1;
    for (uint32_t n = 0; n < 4; n++) {
        uint32_t ps = c + n * 0x234;   /* + 0x84 = the port struct */
        uint16_t m = (uint16_t)(1u << n);
        W16(ps + 0xb4, m);
        for (uint32_t i = 0; i < 16; i++) W16(ps + i * 2 + 0x94, m);
        W32(ps + 0xbc, SEBUF(n));
        W32(ps + 0xb8, s_base + OFS_CHMSGCB0 + n * 0x5c);
        W32(ps + 0xc4, SEBUF(n));
        W32(ps + 0xd4, SEBUF(n));
    }
    return 1;
}

/* 0x2354: re-start the playing ports (none in Versus) */
static void midiSystemStart()
{
    for (int p = 3; p >= 0; p--) {
        if ((R8(s_base + OFS_MIDI_FLAG + (uint32_t)p) & 0xf) != 2) continue;
        sceMidi_MidiPlaySwitch(IEM(), (uint32_t)p, 1);
        sceMidi_MidiSetRelativeTempo(IEM(), (uint32_t)p, R16(s_base + OFS_MIDI_TEMPO + (uint32_t)p * 2));
    }
}

/* 0x23ec */
static void midiSystemStop()
{
    for (int p = 3; p >= 0; p--)
        if ((R8(s_base + OFS_MIDI_FLAG + (uint32_t)p) & 0xf) == 2) sceMidi_MidiPlaySwitch(IEM(), (uint32_t)p, 0);
}

/* 0x2450: after sceMidi_ATick, per port (3..0) the loop word L: -1 = once, the playing flag drops to "stopped" when
 * modmidi flags the sequence end (port struct word +0xc bit 2); else on the end: L == 1 stops, otherwise (0 = forever,
 * n >= 2) rewind + play again and count down */
static void midiAtick()
{
    uint32_t c = IEM();
    sceMidi_ATick(c);
    for (int p = 3; p >= 0; p--) {
        uint32_t lw = c + (uint32_t)p * 4 + 0x954, fl = c + (uint32_t)p + 0x964;
        uint32_t ended = R32(c + (uint32_t)p * 0x234 + 0x90) & 4;
        int32_t L = (int32_t)R32(lw);
        if (L == -1) {
            uint8_t f = R8(fl);
            if ((f & 0xf) == 2 && ended) {
                W8(fl, f & 0xf0);
                W8(fl, R8(fl) | 1);
            }
            continue;
        }
        if (!ended) continue;
        if (L == 1) {
            sceMidi_MidiPlaySwitch(c, (uint32_t)p, 0);
            W8(fl, R8(fl) & 0xf0);
            W8(fl, R8(fl) | 1);
            W32(lw, 0xffffffffu);
            continue;
        }
        sceMidi_SelectMidi(c, (uint32_t)p, 0);
        sceMidi_MidiSetLocation(c, (uint32_t)p, 0);
        sceMidi_MidiPlaySwitch(c, (uint32_t)p, 1);
        sceMidi_MidiSetRelativeTempo(c, (uint32_t)p, R16(c + (uint32_t)p * 2 + 0x968));
        L = (int32_t)R32(lw);
        if (L >= 2) W32(lw, (uint32_t)(L - 1));
    }
}

/* 0x2610: the SQ data pointer goes into input entry 2 * port (via the ctx's entry pointer), the port's stream buffer is
 * emptied; any modmidi error -> 1 */
static int midiLoad(uint32_t port, uint32_t sq)
{
    uint32_t c = IEM();
    W32(R32(c + 0x18) + port * 16 + 4, sq);
    W32(SEBUF(port) + 4, 0);
    if (sceMidi_Load(c, port)) return 1;
    if (sceMidi_SelectMidi(c, port, 0)) return 1;
    if (sceMidi_MidiSetLocation(c, port, 0)) return 1;
    uint32_t fl = s_base + OFS_MIDI_FLAG + port;
    W8(fl, 1);
    if (s_tick.stereo == 0) W8(fl, R8(fl) | 0x10);
    return 0;
}

/* 0x26fc: loop = 0 forever, 1 once, n times; a running / ended port (port struct word +0xc bit 1 / 2) is stopped and
 * rewound first */
static int midiPlay(uint32_t port, uint32_t loop)
{
    uint32_t c = IEM();
    W32(s_base + OFS_MIDI_LOOP + port * 4, loop);
    uint32_t ps = R32(R32(R32(c + 4) + 4) + port * 16 + 0xc);
    uint32_t st = R32(ps + 0xc);
    if ((st & 2) || (st & 4)) {
        sceMidi_MidiPlaySwitch(c, port, 0);
        if (sceMidi_SelectMidi(c, port, 0)) return 1;
        if (sceMidi_MidiSetLocation(c, port, 0)) return 1;
    }
    sceHSyn_ResetAllControler(HCTX(), port);
    int r = sceMidi_MidiPlaySwitch(c, port, 1);
    uint32_t fl = s_base + OFS_MIDI_FLAG + port;
    W8(fl, R8(fl) & 0xf0);
    W8(fl, R8(fl) | 2);
    r |= sceMidi_MidiSetRelativeTempo(c, port, R16(s_base + OFS_MIDI_TEMPO + port * 2));
    return r;
}

/* 0x2848 */
static int midiStop(uint32_t port)
{
    uint32_t c = IEM();
    int r = sceMidi_MidiPlaySwitch(c, port, 0);
    if (sceMidi_SelectMidi(c, port, 0)) return 1;
    if (sceMidi_MidiSetLocation(c, port, 0)) return 1;
    uint32_t fl = s_base + OFS_MIDI_FLAG + port;
    W8(fl, R8(fl) & 0xf0);
    W8(fl, R8(fl) | 1);
    return r;
}

/* 0x28fc (NOT RUN): returns PlaySwitch's result (v0 is not touched after the call) */
static int midiPause(uint32_t port)
{
    int r = sceMidi_MidiPlaySwitch(IEM(), port, 0);
    uint32_t fl = s_base + OFS_MIDI_FLAG + port;
    W8(fl, R8(fl) & 0xf0);
    W8(fl, R8(fl) | 3);
    return r;
}

/* 0x295c (NOT RUN) */
static int midiReplay(uint32_t port)
{
    uint32_t c = IEM();
    int r = sceMidi_MidiPlaySwitch(c, port, 1);
    uint32_t fl = s_base + OFS_MIDI_FLAG + port;
    W8(fl, R8(fl) & 0xf0);
    W8(fl, R8(fl) | 2);
    r |= sceMidi_MidiSetRelativeTempo(c, port, R16(s_base + OFS_MIDI_TEMPO + port * 2));
    return r;
}

/* 0x29ec: 1 while the low nibble is 2 (playing) or 3 (paused) (or any other non-0 / 1 value), else 0 */
static int midiIsPlay(uint32_t port)
{
    uint32_t f = R8(s_base + OFS_MIDI_FLAG + port) & 0xf;
    if (f == 0 || f == 1) return 0;
    return 1;
}

/* 0x2a2c (NOT RUN): reload the port's bank, mono = 0 sets the mono bit (the 5th RPC word, a stack argument); returns
 * sceHSyn_Load's result (v0 untouched after the call) */
static int midiSwitchStereo(uint32_t port, uint32_t bd, uint32_t hd, uint32_t slot, uint32_t mono)
{
    int r = sceHSyn_Load(HCTX(), port, bd, hd, slot);
    uint32_t fl = s_base + OFS_MIDI_FLAG + port;
    if (mono == 0) W8(fl, R8(fl) | 0x10);
    else W8(fl, R8(fl) & 0xf);
    return r;
}

/* 0x2ab4 (NOT RUN): t clamped to [0x1400 / base, 0x40000 / base], base = the absolute tempo's low byte (0 -> 20) */
static int midiRelTempo(uint32_t port, uint16_t t)
{
    uint32_t c = IEM();
    uint16_t s = t;
    int32_t base = sceMidi_MidiGetAbsoluteTempo(c, port) & 0xff;
    if (base <= 0) base = 0x14;
    base &= 0xff;
    int32_t lo = 0x1400 / base;
    if ((int32_t)t < lo) s = (uint16_t)lo;
    int32_t hi = 0x40000 / base;
    if (hi < (int32_t)s) s = (uint16_t)hi;
    int r = sceMidi_MidiSetRelativeTempo(c, port, s);
    W16(s_base + OFS_MIDI_TEMPO + port * 2, s);
    return r;
}

/* ---------------------------------------------------------------- unit tests (selftest; no behaviour) */

static int s_tf, s_tc;
static void tfail(const char *what, uint32_t got, uint32_t want)
{
    s_tf++;
    if (s_tf <= 40) fprintf(stderr, "synth-unit: %s: got %x, want %x\n", what, got, want);
}
#define TEQ(what, got, want) do { s_tc++; uint32_t g_ = (uint32_t)(got), w_ = (uint32_t)(want); if (g_ != w_) tfail(what, g_, w_); } while (0)

static int s_maxv_calls, s_maxv_arg;
static int t_maxv(void *, int n) { s_maxv_calls++; s_maxv_arg = n; return 0; }

/* FlushSe (records, a bad byte, overflow, the 0x408-vs-0x400 room test), SetCslSeStream, GeEeVolumePack on a scratch
 * image at T_BASE; expected values from the asm (0x1134, 0x159c, 0x16f8). Module statics are saved and restored. */
int UnitTest()
{
    constexpr uint32_t T_BASE = 0x100000, T_BUF = 0x118000, T_HD = 0x119000, T_END = 0x11a000;
    s_tf = s_tc = 0;
    uint32_t save_base = s_base;
    int32_t save_fdata = _fdata, save_num = s_iSurroundCalledNum;
    TickBlock save_tick = s_tick;
    Hsyn save_hsyn = s_hsyn;
    memset(iopk::Ptr(T_BASE), 0, T_END - T_BASE);
    s_base = T_BASE;
    _fdata = -1;   /* WaitSema / SignalSema on an unknown id return at once */
    uint32_t sb = T_BASE + OFS_SESTREAM;
    auto stream = [&](const uint8_t *b, uint32_t n) {
        W32(sb, n);
        for (uint32_t k = 0; k < n; k++) W8(sb + 4 + k, b[k]);
    };
    for (uint32_t p = 0; p < 4; p++) {
        W32(SEBUF(p), 0x408);
        W32(SEBUF(p) + 4, 0);
    }

    /* ---- FlushSe: records per port in order, the length word cleared, state 1 after */
    {
        const uint8_t b[] = {0xff, 0, 3, 0x11, 0x12, 0x13, 0xff, 1, 2, 0x21, 0x22, 0xff, 0, 1, 0x14};
        stream(b, sizeof b);
        s_tick.state = 7;
        TEQ("FlushSe ret", FlushSe(), 0);
        TEQ("FlushSe state", s_tick.state, 1);
        TEQ("FlushSe n cleared", R32(sb), 0);
        TEQ("FlushSe p0 used", R32(SEBUF(0) + 4), 4);
        TEQ("FlushSe p0 bytes", R32(SEBUF(0) + 8), 0x14131211);
        TEQ("FlushSe p1 used", R32(SEBUF(1) + 4), 2);
        TEQ("FlushSe p1 bytes", R16(SEBUF(1) + 8), 0x2221);
    }
    /* a non-ff record byte drops the rest of the flush (other ports too) */
    {
        W32(SEBUF(0) + 4, 0);
        W32(SEBUF(1) + 4, 0);
        const uint8_t b[] = {0xff, 0, 2, 0x31, 0x32, 0x7e, 0xff, 1, 1, 0x41};
        stream(b, sizeof b);
        FlushSe();
        TEQ("FlushSe bad p0 used", R32(SEBUF(0) + 4), 2);
        TEQ("FlushSe bad p1 used", R32(SEBUF(1) + 4), 0);
        TEQ("FlushSe bad p1 byte", R8(SEBUF(1) + 8), 0x21);   /* left from the first flush */
        TEQ("FlushSe bad n cleared", R32(sb), 0);
    }
    /* overflow: cap < used + len drops this record and the rest; used + len == cap is taken (the room test ignores
     * the 8-byte header, so the record runs 8 bytes into the next port's header) */
    {
        W32(SEBUF(0) + 4, 0x406);
        W32(SEBUF(1) + 4, 0);
        const uint8_t b[] = {0xff, 0, 3, 1, 2, 3, 0xff, 1, 1, 0x51};
        stream(b, sizeof b);
        FlushSe();
        TEQ("FlushSe full p0 used", R32(SEBUF(0) + 4), 0x406);
        TEQ("FlushSe full p1 used", R32(SEBUF(1) + 4), 0);
        W32(SEBUF(0) + 4, 0x400);
        const uint8_t c[] = {0xff, 0, 8, 0xaa, 0xbb, 0xcc, 0xdd, 0x01, 0x02, 0x03, 0x04};
        stream(c, sizeof c);
        FlushSe();
        TEQ("FlushSe edge p0 used", R32(SEBUF(0) + 4), 0x408);
        TEQ("FlushSe edge p1 cap", R32(SEBUF(1)), 0xddccbbaa);
        TEQ("FlushSe edge p1 used", R32(SEBUF(1) + 4), 0x04030201);
        W32(SEBUF(1), 0x408);
        W32(SEBUF(1) + 4, 0);
        W32(sb, 0);
        W8(sb + 4, 0xff);
        FlushSe();   /* n == 0: nothing */
        TEQ("FlushSe empty p1 used", R32(SEBUF(1) + 4), 0);
    }

    /* ---- SetCslSeStream */
    Hsyn h;
    memset(&h, 0, sizeof h);
    h.SESetMaxVoices = t_maxv;
    s_hsyn = h;
    s_maxv_calls = 0;
    s_maxv_arg = -1;
    {
        uint32_t s0 = T_BASE + OFS_SSCSTAT, s1 = s0 + 0x48;
        TEQ("SetCslSeStream idx 1 first", SetCslSeStream(3, 1), 0xffffffffu);   /* ctx 0's group 1 still zero */
        TEQ("SetCslSeStream idx 1 first maxv", s_maxv_calls, 0);
        TEQ("SetCslSeStream ctx1 grp", R32(s1 + 4), s0 + 0x14);
        TEQ("SetCslSeStream", SetCslSeStream(2, 0), 0);
        TEQ("SetCslSeStream maxv", s_maxv_calls, 1);
        TEQ("SetCslSeStream maxv arg", s_maxv_arg, 0);
        uint32_t hc = T_BASE + OFS_HSYN_CTX, e = hc + 2 * 16, blk = hc + 2 * 0x558;
        TEQ("hsyn entry +1c", R32(e + 0x1c), 0);
        TEQ("hsyn entry +20", R32(e + 0x20), T_BASE + OFS_IEM + 2 * 0x408 + 0x6010);
        TEQ("hsyn entry +28", R32(e + 0x28), blk + 0x11c);
        TEQ("blk +11e", R8(blk + 0x11e), 1);
        TEQ("ssc0 [0]", R32(s0), 2);
        TEQ("ssc0 [4]", R32(s0 + 4), s0 + 0x14);
        TEQ("ssc0 [1c]", R32(s0 + 0x1c), 1);
        TEQ("ssc0 [20]", R32(s0 + 0x20), s0 + 0x24);
        TEQ("ssc0 [28]", R32(s0 + 0x28), SEBUF(2));
        TEQ("SetCslSeStream idx 1", SetCslSeStream(3, 1), 0);   /* validates ctx 0's group (port 2's buffer) */
        TEQ("SetCslSeStream idx 1 maxv", s_maxv_calls, 2);
        TEQ("ssc1 [28]", R32(s1 + 0x28), SEBUF(3));
        W32(SEBUF(2), 7);
        TEQ("SetCslSeStream cap 7", SetCslSeStream(2, 0), 0xffffffffu);
        TEQ("SetCslSeStream cap 7 maxv", s_maxv_calls, 2);
    }

    /* ---- GeEeVolumePack: HD with Prog 0 (volume 100) and Prog 1 absent */
    {
        W32(T_HD + 0x24, 0x40);
        uint32_t pc = T_HD + 0x40;
        W32(pc + 0x10, 0x20);
        W32(pc + 0x14, 0xffffffffu);
        W8(pc + 0x20 + 6, 100);
        uint32_t r = T_BUF;
        auto put = [&](int i, uint16_t l, uint16_t rr, uint16_t prog, uint16_t key, uint32_t hd) {
            uint32_t q = r + (uint32_t)i * 12;
            W16(q, l);
            W16(q + 2, rr);
            W16(q + 4, prog);
            W16(q + 6, key);
            W32(q + 8, hd);
        };
        put(0, 0x2000, 0x4100, 0, 0x42, T_HD);
        put(1, 0x2000, 0x2000, 0, 0x43, 0);
        put(2, 0x2000, 0x2000, 1, 0x44, T_HD);
        put(3, 0xc000, 0x8123, 0, 0x45, T_HD);
        s_iSurroundCalledNum = 0;
        TEQ("Pack ret", GeEeVolumePack(r), 0);
        TEQ("Pack count", s_iSurroundCalledNum, 16);
        uint32_t si = T_BASE + OFS_SIINFO;
        TEQ("Pack 0 key", R32(si), 0x10000042);
        TEQ("Pack 0 left", R32(si + 4), 6450);     /* 0x2000 * 100 / 127 */
        TEQ("Pack 0 right", R32(si + 8), 20069);   /* 0x7fff - (0x7fff - 0x4100) * 100 / 127 */
        TEQ("Pack 1 skipped", R32(si + 16), 0);
        TEQ("Pack 2 skipped", R32(si + 32), 0);
        TEQ("Pack 3 key", R32(si + 48), 0x10000045);
        TEQ("Pack 3 left", R32(si + 52), 0xffffe8d2u);   /* 0x7fff - (0x7fff + 0x4000) * 100 / 127 */
        TEQ("Pack 3 right", R32(si + 56), 229);          /* 0x123 * 100 / 127 */
        s_iSurroundCalledNum = 0x7a;
        GeEeVolumePack(r);
        TEQ("Pack cap count", s_iSurroundCalledNum, 0x7f);
        TEQ("Pack cap 7a key", R32(si + 0x7a * 16), 0x10000042);
        TEQ("Pack cap 7d key", R32(si + 0x7d * 16), 0x10000045);
        GeEeVolumePack(r);
        TEQ("Pack full count", s_iSurroundCalledNum, 0x7f);
    }

    memset(iopk::Ptr(T_BASE), 0, T_END - T_BASE);
    s_base = save_base;
    _fdata = save_fdata;
    s_iSurroundCalledNum = save_num;
    s_tick = save_tick;
    s_hsyn = save_hsyn;
    if (s_tf) fprintf(stderr, "synth-unit: %d of %d checks failed\n", s_tf, s_tc);
    else fprintf(stderr, "synth-unit: %d checks ok\n", s_tc);
    return s_tf;
}

/* host walk of an HD image (bounds-checked; independent of MakeMonoHd / GetHdAdsr): f(3, prog) when a prog index
 * starts, f(0, entry) / f(1, tone) / f(2, smpl entry) for every Prog entry, tone and Smpl entry the original reaches */
template <class F> static void hd_walk(const uint8_t *hd, uint32_t n, F f)
{
    auto u32 = [&](uint32_t o) -> uint32_t { uint32_t v = 0; if (o <= n && n - o >= 4) memcpy(&v, hd + o, 4); return v; };
    auto u16 = [&](uint32_t o) -> uint32_t { return o < n && n - o >= 2 ? (uint32_t)(hd[o] | hd[o + 1] << 8) : 0; };
    auto u8 = [&](uint32_t o) -> uint32_t { return o < n ? hd[o] : 0; };
    uint32_t pg = u32(0x24), ss = u32(0x28), sm = u32(0x2c);
    uint32_t last = u32(pg + 0xc);
    if (last > 0x1000) return;
    for (uint32_t i = 0; i <= last; i++) {
        f(3, i);
        uint32_t off = u32(pg + 0x10 + 4 * i);
        if (off == 0xffffffffu) continue;
        uint32_t e = pg + off;
        uint32_t nt = u8(e + 4);
        if (!nt) continue;
        f(0, e);
        for (uint32_t t = 0, tone = e + u32(e); t < nt; t++, tone += 0x14) {
            f(1, tone);
            uint32_t ssi = u16(tone);
            if (ssi > u32(ss + 0xc)) continue;
            uint32_t se = ss + u32(ss + 0x10 + 4 * ssi);
            for (uint32_t k = 0; k < u8(se + 3); k++) {
                uint32_t si = u16(se + 4 + 2 * k);
                if (si > u32(sm + 0xc)) continue;
                f(2, sm + u32(sm + 0x10 + 4 * si));
            }
        }
    }
}

/* MakeMonoHd / GetHdAdsr / SetHdAdsr on a captured HD (hd, n host bytes; scratch at 0x100000 / 0x140000) */
int HdTest(const uint8_t *hd, uint32_t n)
{
    constexpr uint32_t T_SRC = 0x100000, T_DST = 0x140000, T_MAX = 0x40000;
    if (n < 0x30 || n >= T_MAX) return 0;
    s_tf = s_tc = 0;
    memcpy(iopk::Ptr(T_SRC), hd, n);
    memset(iopk::Ptr(T_DST), 0xee, T_MAX);
    /* MakeMonoHd: the copy with every pan byte the walk reaches centred, nothing else changed */
    static uint8_t want[T_MAX];
    memcpy(want, hd, n);
    int nprog = 0, ntone = 0, nsmpl = 0;
    hd_walk(hd, n, [&](int kind, uint32_t o) {
        if (kind == 0 && o + 0xa < n) want[o + 7] = 0x3f, want[o + 0xa] = 0, nprog++;
        if (kind == 1 && o + 0x11 < n) want[o + 0x11] = 0x3f, ntone++;
        if (kind == 2 && o + 0xd < n) want[o + 0xd] = 0x3f, nsmpl++;
    });
    TEQ("MakeMonoHd ret", MakeMonoHd(T_DST, T_SRC, n), 0);
    const uint8_t *d = iopk::Ptr(T_DST);
    uint32_t bad = 0, first = 0;
    for (uint32_t k = 0; k < n; k++)
        if (d[k] != want[k] && !bad++) first = k;
    TEQ("MakeMonoHd bytes differing", bad, 0);
    if (bad) fprintf(stderr, "synth-unit: MakeMonoHd first difference at +%x: %02x, want %02x\n", first, d[first], want[first]);
    TEQ("MakeMonoHd no overrun", d[n], 0xee);
    TEQ("MakeMonoHd src untouched", memcmp(iopk::Ptr(T_SRC), hd, n), 0);
    /* GetHdAdsr on the original: the last sample reached per prog; SetHdAdsr on the copy, then read back */
    uint32_t cur = 0, got_last = 0;
    bool reached = false;
    int nget = 0;
    auto check_prog = [&](uint32_t p) {
        TEQ("GetHdAdsr", (uint32_t)GetHdAdsr(T_SRC, 0, p), got_last);
        uint32_t v = 0x12345678u ^ (p * 0x01010101u);
        SetHdAdsr(T_DST, 0, p, v);
        TEQ("SetHdAdsr read back", (uint32_t)GetHdAdsr(T_DST, 0, p), reached ? v : 0);
        nget++;
    };
    bool any = false;
    hd_walk(hd, n, [&](int kind, uint32_t o) {
        if (kind == 3) {
            if (any) check_prog(cur);
            any = true;
            cur = o;
            got_last = 0;
            reached = false;
        }
        if (kind == 2) reached = true;
        if (kind == 2 && o + 0x16 <= n)
            got_last = (uint32_t)(hd[o + 0x12] | hd[o + 0x13] << 8) << 16 | (uint32_t)(hd[o + 0x14] | hd[o + 0x15] << 8);
    });
    if (any) check_prog(cur);
    memset(iopk::Ptr(T_SRC), 0, n);
    memset(iopk::Ptr(T_DST), 0, T_MAX);
    if (s_tf) fprintf(stderr, "synth-unit: HD %x bytes: %d of %d checks failed\n", n, s_tf, s_tc);
    else
        fprintf(stderr, "synth-unit: HD %x bytes: MakeMonoHd ok (%d progs, %d tones, %d samples centred), Get / SetHdAdsr ok on %d progs\n",
                n, nprog, ntone, nsmpl, nget);
    return s_tf;
}

}  // namespace synth
