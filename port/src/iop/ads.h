/* ads: FromSoftware's ADS BGM streamer ("fssdads", IRX.BIN@04d800, crc a3367fc5) decompiled onto the native IOP kernel
 * iopk + native sd. fssd module 5: streams a raw-PCM file
 * (\ADS\ADSnnnn.ADX, 48 kHz stereo, interleave 0x200) from the disc by LSN through the two halves of one IOP buffer into
 * core 1 AutoDMA. Two private worker threads: ProcLsn (prio 0x52, one refill per ADMA-done interrupt) and SetProcLsn
 * (prio 0x54, the setup reads, one step per EE Sync). The wake order is carried by thread priorities juggled inside the
 * RPC handlers (ChangeThreadPriority 0x56 / 0x19 around WakeupThread = iopk slice boundaries, kept call for call).
 *
 * Nothing EE-visible lives in the image (the EE sees only the 4-byte replies, copied by fssd into its shared reply
 * buffer, and the stream buffer address as sd's ADMA madr): all module state is host statics, zero at module start like
 * the zeroed image - the original relies on that (adsSystemInit is never called: fd 0, volumes 0, CdRead mode {0,0,0}).
 * The stream buffer is an AllocSysMemory block in iopk::Ram().
 *
 * State of the port: all 29 functions native (NI5b G2 + G2b). The file variant (adsOpen 0x19ac, adsSetup 0x1bec and
 * their ioman workers adsProcFile 0x62c / adsSetProcFile 0xa50) is never sent by the EE; --snd-selftest drives it by
 * replaying the recording a second time with the SetupWithLsn RPC swapped for SetBuff + Setup(file) (check 7). */
#pragma once
#include <stdint.h>
#include "../../platform/snd/iop/iopk.h"

namespace ads {

constexpr uint32_t IMAGE_SIZE = 0x2d30;
constexpr int MODULE = 5;   /* fssd module number (fno >> 28) */

int ModuleStart(const iopk::ModuleArgs *m);   /* `start` (0x220): fsiopsdRegister(5, RpcCallBack), returns 0 */

/* test view (selftest assertions; no behaviour) */
struct View {
    int proc_tid, proc_alive, setproc_tid, setproc_alive;
    int state[2];
    uint32_t buf[2];
    uint32_t refills;          /* ProcLsn refill reads issued (normal + tail branch) */
    uint32_t last_fill;        /* IOP address of the half the last refill read into */
};
void GetView(View *v);

}  // namespace ads
