/* iophost - see iophost.h */
#include "iophost.h"
#include "fssd.h"
#include "evag.h"
#include "sdr.h"
#include "ads.h"
#include "synth.h"
#include "mfstrm.h"
#include "ivag.h"
#include "mvag.h"
#include "../../platform/snd/iop/sd.h"
#include "../../platform/snd/iop/hsyn.h"
#include "../../platform/snd/iop/midi.h"
#include "../../platform/snd/iop/sein.h"
#include "../../platform/snd/mixer.h"
#include "../../platform/plat.h"
#include <string.h>

namespace iophost {

static SndMixer *g_mix;
static RegTrace g_trace;
static void *g_trace_user;
static RegTrace g_rtrace;
static RegTrace g_ptrace;
static void *g_ptrace_user;
static void *g_rtrace_user;
static DmaTrace g_dtrace;
static void *g_dtrace_user;
static AdmaTrace g_atrace;
static void *g_atrace_user;

static MixTap g_mtap;
static void *g_mtap_user;
static void spu_run(void *, int samples)
{
    if (!g_mtap) { snd_mixer_mix(g_mix, 0, samples); return; }
    int16_t buf[2 * 256];
    while (samples > 0) {
        int n = samples > 256 ? 256 : samples;
        snd_mixer_mix(g_mix, buf, n);
        g_mtap(g_mtap_user, buf, n);
        samples -= n;
    }
}
void SetMixTap(MixTap fn, void *user)
{
    g_mtap = fn;
    g_mtap_user = user;
}
static void spu_tick(void *) { snd_mixer_service(g_mix); }

/* mixer events: DMA end -> the channel's CHCR busy bit clears, then IRQ 36 / 40; SPU2 IRQ -> IRQ 9 */
static void mix_event(void *, int ev, int core)
{
    if (ev == SND_EV_DMA_DONE) {
        sd::DmaEnd(core);
        iopk::RaiseIrq(core ? 40 : 36);
    } else if (ev == SND_EV_IRQ) {
        iopk::RaiseIrq(9);
    }
}

/* AutoDMA input: madr = the IOP address sd programmed */
static void mix_adma(void *, int core, uint32_t madr, void *dst, uint32_t n)
{
    uint8_t *src = iopk::Ptr(madr);
    if (src && (madr & 0x1fffff) + (uint64_t)n <= iopk::RAM_SIZE) memcpy(dst, src, n);
    else memset(dst, 0, n);
    if (g_atrace) g_atrace(g_atrace_user, core, madr, dst, n);
}

static void sd_trace(void *, int kind, uint32_t reg, uint32_t val)
{
    if (kind == 'W' && g_trace) g_trace(g_trace_user, reg, (uint16_t)val);
    else if (kind == 'R' && g_rtrace) g_rtrace(g_rtrace_user, reg, (uint16_t)val);
}

static void sd_pre_write(void *, uint32_t reg, uint32_t val)
{
    if (g_ptrace) g_ptrace(g_ptrace_user, reg, (uint16_t)val);
}

void SetPreWriteTrace(RegTrace fn, void *user)
{
    g_ptrace = fn;
    g_ptrace_user = user;
}

void SetReadTrace(RegTrace fn, void *user)
{
    g_rtrace = fn;
    g_rtrace_user = user;
}

static void sd_trace_dma(void *, int core, uint32_t madr, uint32_t chcr, uint32_t bytes, const void *data)
{
    if (g_dtrace) g_dtrace(g_dtrace_user, core, madr, bytes, data == 0, chcr, data);
}

void SetDmaTrace(DmaTrace fn, void *user)
{
    g_dtrace = fn;
    g_dtrace_user = user;
}

void SetAdmaTrace(AdmaTrace fn, void *user)
{
    g_atrace = fn;
    g_atrace_user = user;
}

void Init(SndMixer *m, const iopk::Hooks *ee, RegTrace trace, void *trace_user)
{
    g_mix = m;
    g_trace = trace;
    g_trace_user = trace_user;
    iopk::Reset();
    iopk::Hooks h;
    memset(&h, 0, sizeof h);
    if (ee) h = *ee;
    h.spu_run = spu_run;
    h.spu_tick = spu_tick;
    h.spu_next_event = 0;
    iopk::SetHooks(&h);
    snd_mixer_set_callbacks(m, mix_event, mix_adma, 0);
}

/* LIBSD: the library is linked, not loaded; its module start = state reset + attach (zero page = LIBSD rodata D_04d40
 * at image + 0x4d40, zero in the phantom image) + the original entry */
static int libsd_start(const iopk::ModuleArgs *a)
{
    sd::Hw hw;
    memset(&hw, 0, sizeof hw);
    hw.mixer = g_mix;
    hw.sync = iopk::SdHwSync;
    hw.trace = sd_trace;
    hw.trace_dma = sd_trace_dma;
    hw.pre_write = sd_pre_write;
    hw.iop_ram = iopk::Ram();
    hw.iop_ram_size = iopk::RAM_SIZE;
    hw.zero_page = a->base + 0x4d40;
    sd::ResetModuleState();
    sd::Attach(iopk::SdKernel(), &hw);
    return sd::ModuleStart();
}

struct Mod { const char *name; uint32_t size; iopk::ModuleStartFn fn; };
static const Mod MODS[] = {
    {"Sound_Device_Library", 0x5980, libsd_start},
    {"Midi_Sequencer_Module", midi::IMAGE_SIZE, midi::ModuleStart},
    {"SPU2_Synthesizer_Module", hsyn::IMAGE_SIZE, hsyn::ModuleStart},
    {"SE_Sequence_Input_Module", sein::IMAGE_SIZE, sein::ModuleStart},
    {"fssd", fssd::IMAGE_SIZE, fssd::ModuleStart},
    {"fssdads", ads::IMAGE_SIZE, ads::ModuleStart},
    {"fssdsyth", synth::IMAGE_SIZE, synth::ModuleStart},
    {"sdr_driver", sdr::IMAGE_SIZE, sdr::ModuleStart},
    {"fssdevag", evag::IMAGE_SIZE, evag::ModuleStart},
    {"fssdmvag", mvag::IMAGE_SIZE, mvag::ModuleStart},   /* never loaded by the game (isLoad 0); IRX name assumed */
    {"FsMFStrm", mfstrm::IMAGE_SIZE, mfstrm::ModuleStart},
    {"fsiopsdivag", ivag::IMAGE_SIZE, ivag::ModuleStart},
    {"ck01", 0x90, 0},
};

/* modhsyn behind SYTH's / fssd's interface structs (NI6b H1 - H3: every entry SYTH / fssd call is native) */
static int hs_init(void *, uint32_t ctx, uint32_t usec) { return hsyn::Init(ctx, usec); }
static int hs_reserve(void *, const uint32_t mask[2]) { return hsyn::SetReservVoice(mask); }
static int hs_atick(void *, uint32_t ctx) { return hsyn::ATick(ctx); }
static int hs_setvol(void *, uint32_t ctx, uint32_t port, uint16_t v) { return hsyn::SetVolume(ctx, port, v); }
static int hs_allnoteoff(void *, uint32_t ctx, uint32_t port) { return hsyn::AllNoteOff(ctx, port); }
static int hs_resetctl(void *, uint32_t ctx, uint32_t port) { return hsyn::ResetAllControler(ctx, port); }
static int hs_statbuf(void *, uint32_t buf) { return hsyn::SetVoiceStatBuffer(buf); }
static int hs_load(void *, uint32_t ctx, uint32_t port, uint32_t bd, uint32_t hd, uint32_t slot) { return hsyn::Load(ctx, port, bd, hd, slot); }
static int hs_vtrans(void *, int ch, uint32_t iop, uint32_t spu, uint32_t size) { return hsyn::VoiceTrans((uint32_t)ch, iop, spu, size); }
static int hs_effect(void *, uint32_t rec) { return hsyn::SetEffectAttr(rec); }
static int hs_chstat(void *, uint32_t ctx, uint32_t port, uint32_t buf) { return hsyn::GetChStat(ctx, port, buf); }
static int hs_outmode(void *, int m) { return hsyn::SetOutputMode(m); }
static int hs_semax(void *, int n) { return hsyn::SESetMaxVoices((uint32_t)n); }
static int hs_seretr(void *, uint32_t ctx, uint32_t port, uint32_t id, uint32_t buf, int n)
{
    return hsyn::SERetrieveVoiceNumberByID(ctx, port, id, buf, n);
}
static int hs_msget(void *, uint32_t ctx, uint32_t port, uint32_t id, uint32_t buf, int n)
{
    return hsyn::MSGetVoiceStateByID(ctx, port, id, buf, n);
}

void HsynNative(synth::Hsyn *sh, fssd::Hsyn *fh)
{
    if (sh) {
        sh->ATick = hs_atick;
        sh->SetVolume = hs_setvol;
        sh->AllNoteOff = hs_allnoteoff;
        sh->ResetAllControler = hs_resetctl;
        sh->SetVoiceStatBuffer = hs_statbuf;
        sh->Load = hs_load;
        sh->VoiceTrans = hs_vtrans;
        sh->SetEffectAttr = hs_effect;
        sh->GetChStat = hs_chstat;
        sh->SetOutputMode = hs_outmode;
        sh->SESetMaxVoices = hs_semax;
        sh->SERetrieveVoiceNumberByID = hs_seretr;
        sh->MSGetVoiceStateByID = hs_msget;
    }
    if (fh) {
        fh->Init = hs_init;
        fh->SetReservVoice = hs_reserve;
    }
}

/* modmidi behind SYTH's interface struct (NI7 W1) */
static int md_init(void *, uint32_t ctx, uint32_t usec) { return midi::Init(ctx, usec); }
static int md_atick(void *, uint32_t ctx) { return midi::ATick(ctx); }
static int md_load(void *, uint32_t ctx, uint32_t port) { return midi::Load(ctx, port); }
static int md_select(void *, uint32_t ctx, uint32_t port, uint32_t n) { return midi::SelectMidi(ctx, port, n); }
static int md_play(void *, uint32_t ctx, uint32_t port, uint32_t on) { return midi::MidiPlaySwitch(ctx, port, on); }
static int md_loc(void *, uint32_t ctx, uint32_t port, uint32_t t) { return midi::MidiSetLocation(ctx, port, t); }
static int md_abstempo(void *, uint32_t ctx, uint32_t port) { return midi::MidiGetAbsoluteTempo(ctx, port); }
static int md_reltempo(void *, uint32_t ctx, uint32_t port, uint16_t t) { return midi::MidiSetRelativeTempo(ctx, port, t); }

/* the port structs' +0x34 callback: SYTH's chMsgCB_n at its image offset 0x1ff4 + 0x5c n (synth.h ChMsgCB) */
static uint32_t md_chmsg(void *, uint32_t cb, uint32_t msg, uint32_t)
{
    uint32_t o = cb - synth::Base() - 0x1ff4;
    if (synth::Base() && o % 0x5c == 0 && o / 0x5c < 4) return synth::ChMsgCB((int)(o / 0x5c), msg);
    static bool warned;
    if (!warned) plat_log("iophost: modmidi channel callback %06x is not a SYTH chMsgCB (passed)", cb);
    warned = true;
    return msg;
}

void MidiNative(synth::Midi *m)
{
    m->Init = md_init;
    m->ATick = md_atick;
    m->Load = md_load;
    m->SelectMidi = md_select;
    m->PlaySwitch = md_play;
    m->SetLocation = md_loc;
    m->GetAbsoluteTempo = md_abstempo;
    m->SetRelativeTempo = md_reltempo;
    midi::SetChMsgResolver(md_chmsg, 0);
}

iopk::ModuleStartFn NativeStart(const char *name, uint32_t *image_size)
{
    for (const Mod &m : MODS)
        if (!strcmp(m.name, name)) {
            if (image_size) *image_size = m.size;
            return m.fn;
        }
    return 0;
}

const char *NameBySize(uint32_t image_size)
{
    for (const Mod &m : MODS) if (m.size == image_size) return m.name;
    return 0;
}

static synth::Hsyn w_sh;
static fssd::Hsyn w_fh;
static synth::Midi w_m;
void WireNative()
{
    memset(&w_sh, 0, sizeof w_sh);
    memset(&w_fh, 0, sizeof w_fh);
    memset(&w_m, 0, sizeof w_m);
    HsynNative(&w_sh, &w_fh);
    fssd::SetHsyn(&w_fh);
    synth::SetHsyn(&w_sh);
    MidiNative(&w_m);
    synth::SetMidi(&w_m);
}

static void host_init(SndMixer *m, const iopk::Hooks *ee)
{
    Init(m, ee, 0, 0);
    WireNative();
}

const sndhost::Binding *HostBinding()
{
    static const sndhost::Binding b = {host_init, NativeStart, SetMixTap, NameBySize};
    return &b;
}

}  // namespace iophost
