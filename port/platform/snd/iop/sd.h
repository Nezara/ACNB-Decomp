/* sd: Sony's IOP sound device library (LIBSD "Sound_Device_Library" v0303, the sceSd* API) decompiled into native C++
 *. The only producer of mixer commands: every SPU2 register store
 * of the original becomes a RegWrite (decoded onto port/platform/snd/mixer.h through a small register shadow), every load a
 * RegRead, in the original order (the order is the product: the NI0e oracle compares it record by record).
 *
 * Generic (no game addresses): kernel services come through sd::Kernel (the IOP intrman / thevent calls of the original;
 * implemented by the native IOP kernel iopk (NI4), mocked by --sd-selftest), the mixer and the host clock through sd::Hw.
 * Time: sd never advances the mixer itself. Hw::sync is called before every SPU2 register access (and, from P2 on, every
 * DMA start); the host must run the mixer up to the IOP's current time there (iopk: 16 * floor(T / 12288) samples, design
 * 4 "Timing model"; the selftest: the log record's sample position). Interrupts: sd registers its handlers with
 * Kernel::RegisterIntrHandler (IRQ 9 = SPU2 IRQ, 36 / 40 = core 0 / 1 DMA end, as on the IOP); the host calls them when the
 * mixer raises SND_EV_IRQ / SND_EV_DMA_DONE, with interrupts "masked" (QueryIntrContext() != 0 while they run).
 * One sd per process (module state = the original's .data / .bss); not thread-safe (IOP threads are cooperative).
 *
 * State of the port: group P1 (register API, Note2Pitch / Pitch2Note, core attributes, handler setters, IRQ-9 handler,
 * Init / Quit), P2 (DMA channels: VoiceTrans / BlockTrans and their status, StopTrans, the DMA-end ISR) and P3 (reverb
 * effects: SetEffectAttr / GetEffectAttr / SetEffectMode / SetEffectModeParams, Clear / CleanEffectWorkArea, the 10
 * presets) and P4 (ProcBatch / ProcBatchEx command lists, incl. their SIF DMA to the EE) are native: the whole library.
 * Transfers: sd's API takes host pointers into the IOP RAM image (Hw::iop_ram); internally they become 32-bit IOP
 * addresses (MADR) as on the IOP, so BlockTransStatus returns IOP addresses and the mixer's AutoDMA "madr" cookies are
 * IOP addresses (the host's SndMixerAdmaFn maps them back to iop_ram). DMA end: when the mixer raises SND_EV_DMA_DONE for
 * a core (sample-memory DMA serviced, or the last block of an AutoDMA span pulled), the host calls DmaEnd(core) (the
 * channel's CHCR busy bit clears) and then dispatches IRQ 36 (core 0) / 40 (core 1) to the handler sd registered
 * (DmaIsr). VoiceTransStatus(ch, 1) is the one sd call that blocks (Kernel::WaitEventFlag until the ISR's iSetEventFlag). */
#pragma once
#include <stdint.h>

struct SndMixer;

namespace sd {

typedef int (*IntrHandler)(int core, void *arg);   /* sceSdTransIntrHandler (core) / sceSdSpu2IntrHandler (core mask) */
typedef int (*Callback)(void *data);               /* SetTransCallback / SetIRQCallback (called with 0) */
typedef int (*KIntrHandler)(void *arg);            /* kernel-level interrupt handler (RegisterIntrHandler) */

struct EventFlagParam { uint32_t attr, option, init; };

/* sifman SceSifDmaData (IOP -> EE): src is sd's host pointer (IOP side), dest the EE address, size in bytes */
struct SifDmaTransfer { const void *src; uint32_t dest; uint32_t size; uint32_t attr; };

/* sceSdBatch: one ProcBatch / ProcBatchEx command (8 bytes as on the IOP): cmd 1-4 SetParam / SetSwitch / SetAddr /
 * SetCoreAttr (entry, value), 5 store entry at the IOP address value, 6 / 7 SIF DMA (entry / the ret buffer, entry bytes)
 * to the EE address value, 0x11-0x14 GetParam / GetSwitch / GetAddr / GetCoreAttr (entry) */
struct BatchEntry { uint16_t cmd, entry; uint32_t value; };

/* IOP kernel services (intrman / thevent); u = Kernel::user */
struct Kernel {
    void *user;
    int (*CpuSuspendIntr)(void *u, int *state);
    int (*CpuResumeIntr)(void *u, int state);
    int (*QueryIntrContext)(void *u);
    int (*RegisterIntrHandler)(void *u, int irq, int mode, KIntrHandler fn, void *arg);
    int (*ReleaseIntrHandler)(void *u, int irq);
    int (*EnableIntr)(void *u, int irq);
    int (*DisableIntr)(void *u, int irq, int *res);
    int (*CreateEventFlag)(void *u, const EventFlagParam *p);
    int (*DeleteEventFlag)(void *u, int id);
    int (*SetEventFlag)(void *u, int id, uint32_t bits);
    int (*iSetEventFlag)(void *u, int id, uint32_t bits);
    int (*ClearEventFlag)(void *u, int id, uint32_t bits);
    int (*iClearEventFlag)(void *u, int id, uint32_t bits);
    int (*WaitEventFlag)(void *u, int id, uint32_t bits, int mode, uint32_t *res);
    void (*FlushDcache)(void *u);
    int (*SifSetDma)(void *u, const SifDmaTransfer *t, int n);   /* queue id, 0 = queue full (ProcBatch commands 6 / 7 only) */
    int (*SifDmaStat)(void *u, int id);                           /* >= 0 while the transfer is pending, < 0 when done */
};

/* SPU2 side: the mixer + host hooks (both hooks optional) */
struct Hw {
    SndMixer *mixer;
    void *user;
    void (*sync)(void *u);                                         /* before an access: run the mixer up to "now" */
    void (*trace)(void *u, int kind, uint32_t reg, uint32_t val);  /* after an access: 'W' (val written) / 'R' (val read) */
    /* after a DMA start (CHCR store with the start bit): core, IOP address, CHCR value, bytes, the IOP RAM bytes (0 for
     * AutoDMA: the mixer pulls them) */
    void (*trace_dma)(void *u, int core, uint32_t madr, uint32_t chcr, uint32_t bytes, const void *data);
    uint8_t *iop_ram;          /* IOP RAM image: IOP address 0 (transfers outside it run with size 0, as the hybrid) */
    uint32_t iop_ram_size;
    /* IOP address of a 0x400-byte all-zero block in iop_ram (the original's LIBSD rodata D_04d40, base + 0x4d40 in the
     * hybrid): Clear / CleanEffectWorkArea DMA it over the reverb work area, so the host must keep it zero */
    uint32_t zero_page;
    /* before a register store (after sync): reg, the value about to be written (the mixer still holds the old state);
     * optional, appended last so positional initialisers stay valid */
    void (*pre_write)(void *u, uint32_t reg, uint32_t val);
};

/* sceSdEffectAttr: mode = mode 0..9 (low byte) | 0x100 clear the work area | 0x200 clear through DMA channel 1 */
struct EffectAttr { int32_t core; uint32_t mode; uint16_t depth_l, depth_r; uint32_t delay, feedback; };

void Attach(const Kernel *k, const Hw *hw);
void ResetModuleState();   /* the state right after the module is loaded (init data + f_4570), no kernel calls */
int ModuleStart();         /* module entry f_00b0 without RegisterLibraryEntries (sd is linked, not loaded) */

/* SPU2 register layer (reg = offset from the SPU2 base, 0..0x7fe): register-file shadow + mixer commands / queries.
 * Public for the selftest and for the not yet ported groups' log-driven replay. */
void RegWrite(uint32_t reg, uint16_t v);
uint16_t RegRead(uint32_t reg);

/* API (export index in the original's table) */
int Init(int flag);                                          /* 4 */
int Quit();                                                  /* 2 (module stop) */
void SetParam(uint16_t entry, uint16_t value);               /* 5 */
uint16_t GetParam(uint16_t entry);                           /* 6 */
void SetSwitch(uint16_t entry, uint32_t value);              /* 7 */
uint32_t GetSwitch(uint16_t entry);                          /* 8 */
void SetAddr(uint16_t entry, uint32_t value);                /* 9 */
uint32_t GetAddr(uint16_t entry);                            /* 10 */
int SetCoreAttr(uint16_t entry, uint16_t value);             /* 11 (void in Sony's header; returns the original's v0) */
uint16_t GetCoreAttr(uint16_t entry);                        /* 12 */
uint16_t Note2Pitch(uint16_t center_note, uint16_t center_fine, uint16_t note, int16_t fine);   /* 13 */
uint16_t Pitch2Note(uint16_t center_note, uint16_t center_fine, uint16_t pitch);              /* 14 */
int ProcBatch(const BatchEntry *batch, uint32_t *ret, uint32_t num);                           /* 15 */
int ProcBatchEx(const BatchEntry *batch, uint32_t *ret, uint32_t num, uint32_t voice);         /* 16 */
int VoiceTrans(int ch, uint16_t mode, void *iop, uint32_t spu_addr, uint32_t size);           /* 17 */
int BlockTrans(int ch, uint16_t mode, void *iop, uint32_t size, void *start);                 /* 18 (start: mode 3) */
int VoiceTransStatus(int ch, int wait);                                                        /* 19 */
uint32_t BlockTransStatus(int ch);                                                             /* 20 */
Callback SetTransCallback(int ch, Callback fn);              /* 21 */
Callback SetIRQCallback(Callback fn);                        /* 22 */
int SetEffectAttr(int core, const EffectAttr *attr);         /* 23 */
int GetEffectAttr(int core, EffectAttr *attr);               /* 24 */
int ClearEffectWorkArea(int core, int ch, int mode);         /* 25 */
IntrHandler SetTransIntrHandler(int ch, IntrHandler fn, void *arg);   /* 26 */
IntrHandler SetSpu2IntrHandler(IntrHandler fn, void *arg);            /* 27 */
void *GetTransIntrHandlerArgument(int ch);                   /* 28 */
void *GetSpu2IntrHandlerArgument();                          /* 29 */
int StopTrans(int ch);                                       /* 30 */
int CleanEffectWorkArea(int core, int ch, int mode);         /* 31 */
int SetEffectMode(int core, const EffectAttr *attr);         /* 32 */
int SetEffectModeParams(int core, const EffectAttr *attr);   /* 33 */

/* the interrupt handlers Init registers (exposed so a host / test can identify them) */
int Spu2Isr(void *arg);   /* IRQ 9 (LIBSD 0x3ee0) */
int DmaIsr(void *cookie); /* IRQ 36 / 40 (f_27b8) */
/* hardware side of a DMA / AutoDMA span end (before IRQ 36 / 40): the channel's CHCR busy bit clears */
void DmaEnd(int core);

}  // namespace sd

/* --sd-selftest <spurec log>: every NI0e C record of a native export (and every IRQ 9 / 36 / 40 ISR entry) is
 * run through native sd against a native mixer driven by the log; sd's W / R records, V return values, F kernel calls and
 * K handler calls are compared with the log in order (the rest of the log drives the mixer and sd's register shadow).
 * ProcBatch / ProcBatchEx run natively from the NI0f oracle logs (the recorder captures the command list since NI0f; no game
 * calls them) and are also covered by a built-in unit test run after the log: the same command lists vs the single calls.
 * Prints per-export calls / mismatches to stderr; returns 0 if nothing mismatched. */
extern "C" int snd_sd_selftest(const char *log);
