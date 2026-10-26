/* lib/sdk/libvifpk.cpp - Sony libvifpk (SDK 2.7, main ELF 0x139038-0x139334, no DWARF): the VIF1 packet writer
 * (sceVif1Packet = DWARF struct_3497d839 {pCurrent, pBase, pDmaTag, pGifTag}) - DMA tags (CNT / REF / END) whose QWC
 * Terminate patches afterwards, GIF tags whose NLOOP CloseGifTag fills in, A+D pairs. Only the FMV player's disp.c
 * (clearGsMem, setImageTag, interpreted) uses it; registered at the SDK addresses by runtime/bridge_sdk.c.
 * Pure memory code on 32-bit packet addresses: the cursor arithmetic and the stored pointers keep the guest values
 * (uncached / UCAB mirrors stay as they are), writes go through HOSTPTR. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/types.h"
#include <string.h>

/* sceVif1PkInit (0x139038): pGifTag is left as it is */
void sceVif1PkInit(sceVif1Packet *pk, void *base)
{
    pk->pCurrent = (unsigned int *)base;
    pk->pDmaTag = 0;
    pk->pBase = (u_long128 *)base;
}

/* sceVif1PkReset (0x139048) */
void sceVif1PkReset(sceVif1Packet *pk)
{
    pk->pCurrent = (unsigned int *)pk->pBase;
    pk->pDmaTag = 0;
}

static unsigned int *vifpk_w(unsigned int a) { return (unsigned int *)HOSTPTR(a); }

/* pads the cursor with zero words up to a qword boundary (tests address bits 2-3 only: the cursor is always
 * word-aligned) */
static unsigned int vifpk_pad(unsigned int p)
{
    while (p & 0xc) {
        *vifpk_w(p) = 0;
        p += 4;
    }
    return p;
}

/* sceVif1PkTerminate (0x139058): pads to a qword, adds the data qwords after the open DMA tag to its QWC word
 * (byte distance >> 4 arithmetic, minus the tag itself), closes the tag; returns the cursor */
unsigned int *sceVif1PkTerminate(sceVif1Packet *pk)
{
    unsigned int tag = PS2PTR(pk->pDmaTag);
    unsigned int p = vifpk_pad(PS2PTR(pk->pCurrent));
    if (tag) *vifpk_w(tag) += (unsigned int)(((int)(p - tag) >> 4) - 1);
    pk->pDmaTag = 0;
    pk->pCurrent = (unsigned int *)(uintptr_t)p;
    return (unsigned int *)(uintptr_t)p;
}

/* DMA tag qword at the cursor: {w0, w1, vif0, vif1}, cursor += 16 */
static void vifpk_tag(sceVif1Packet *pk, unsigned int w0, unsigned int w1, unsigned int vif0, unsigned int vif1)
{
    unsigned int p = PS2PTR(pk->pCurrent);
    unsigned int *q = vifpk_w(p);
    q[0] = w0;
    q[1] = w1;
    q[2] = vif0;
    q[3] = vif1;
    pk->pCurrent = (unsigned int *)(uintptr_t)(p + 16);
}

/* sceVif1PkCnt (0x1390b0): opens a CNT tag {qwc | ID 1, 0, vif0, vif1} at the aligned cursor (QWC grows by Terminate) */
void sceVif1PkCnt(sceVif1Packet *pk, unsigned int vif0, unsigned int vif1, unsigned int qwc)
{
    PS2_FRAME(0x50);
    unsigned int *tag = sceVif1PkTerminate(pk);
    pk->pDmaTag = (u_long128 *)tag;
    vifpk_tag(pk, qwc | 0x10000000u, 0, vif0, vif1);
}

/* sceVif1PkRef (0x139128): a REF tag {flags | qwc | ID 3, addr without bits 29-30 (bit 31 = SPR stays), vif0, vif1};
 * leaves pDmaTag closed (the referenced data is not in the packet) */
void sceVif1PkRef(sceVif1Packet *pk, void *addr, unsigned int qwc, unsigned int vif0, unsigned int vif1, unsigned int flags)
{
    PS2_FRAME(0x70);
    sceVif1PkTerminate(pk);
    vifpk_tag(pk, flags | (qwc | 0x30000000u), PS2_DMA_ADDR(addr, PS2PTR(addr) & 0x9fffffffu), vif0, vif1);
}

/* sceVif1PkEnd (0x1391c0): like Cnt with ID 7 (END) */
void sceVif1PkEnd(sceVif1Packet *pk, unsigned int vif0, unsigned int vif1, unsigned int qwc)
{
    PS2_FRAME(0x50);
    unsigned int *tag = sceVif1PkTerminate(pk);
    pk->pDmaTag = (u_long128 *)tag;
    vifpk_tag(pk, qwc | 0x70000000u, 0, vif0, vif1);
}

/* sceVif1PkReserve (0x139238): skips n words */
void sceVif1PkReserve(sceVif1Packet *pk, unsigned int n)
{
    pk->pCurrent = (unsigned int *)(uintptr_t)(PS2PTR(pk->pCurrent) + n * 4);
}

/* sceVif1PkOpenGifTag (0x139250): `sq` of the tag at the cursor (aligned down like sq), remembers it in pGifTag */
void sceVif1PkOpenGifTag(sceVif1Packet *pk, const void *tag)
{
    unsigned int p = PS2PTR(pk->pCurrent);
    memcpy(vifpk_w(p & ~0xfu), tag, 16);
    pk->pCurrent = (unsigned int *)(uintptr_t)(p + 16);
    pk->pGifTag = (unsigned long long *)(uintptr_t)p;
}

/* sceVif1PkCloseGifTag (0x139268): adds NLOOP to the open GIF tag (assumes it was 0): the dwords after the tag, halved
 * (logical shift) unless FLG = 1 (REGLIST counts dwords; PACKED / IMAGE / disabled count qwords), and except for FLG = 2
 * (IMAGE) divided by NREG (0 = 16) rounding up (unsigned); then pads the cursor to a qword */
void sceVif1PkCloseGifTag(sceVif1Packet *pk)
{
    unsigned int t = PS2PTR(pk->pGifTag);
    unsigned long long *tp = (unsigned long long *)HOSTPTR(t);
    unsigned long long tag = *tp;
    unsigned int cur = PS2PTR(pk->pCurrent);
    unsigned int n = (unsigned int)(((int)(cur - t) >> 3) - 2);
    unsigned int flg = (unsigned int)(tag >> 58) & 3;
    if (flg != 1) n >>= 1;
    if (flg != 2) {
        unsigned int nreg = (unsigned int)(tag >> 60);
        if (nreg == 0) nreg = 16;
        n = (n + nreg - 1) / nreg;
    }
    pk->pGifTag = 0;
    *tp = tag + n;
    pk->pCurrent = (unsigned int *)(uintptr_t)vifpk_pad(cur);
}

/* sceVif1PkAddGsAD (0x139310): A+D pair - the data dword first, then the register address (zero-extended) */
void sceVif1PkAddGsAD(sceVif1Packet *pk, unsigned int reg, unsigned long long data)
{
    unsigned int p = PS2PTR(pk->pCurrent);
    unsigned long long *q = (unsigned long long *)HOSTPTR(p);
    q[0] = data;
    q[1] = reg;
    pk->pCurrent = (unsigned int *)(uintptr_t)(p + 16);
}
