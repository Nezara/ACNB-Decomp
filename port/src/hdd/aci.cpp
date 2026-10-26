/* src/hdd/aci.cpp - parts-file attribute tables: for each AC parts kind (AcLoader m_kind 0..13: head, core, arm, leg,
 * booster, FCS, generator, radiator, inside, extension, back weapon, right / left hand weapon, option) the list of
 * files of one part ({usAtrb = file attribute id in the parts pack (1 .prm, 2 .wsc, 3-5 near / far / shadow .omd,
 * ...), HDD file-name format, usDvdId (always 0)}, ended by a NULL format). Only the HDD loader arm reads the
 * entries; on the disc path acloader just stores the pointer.
 * The tables are file-name maps (facts, user decision s91) and stay in code: the hybrid returns the ELF copies (DWARF
 * globals, guest memory), the source port defines them here. Original copy-paste kept: the Extension table's texture
 * format is "bo%03d.tmx" (the booster's). */
#include "acnb/port.h"

#ifdef ACNB_SOURCEPORT
#define FI(atrb, fmt) {(unsigned short)(atrb), {0, 0}, (char *)(fmt), 0, {0, 0}}
#define FI_END {0, {0, 0}, 0, 0, {0, 0}}
struct StPartsFileAtrbInfo s_fiPartsHead[9] = {
    FI(1, "chd%03d.prm"), FI(2, "chd%03d.wsc"), FI(3, "ahd%03d_n.omd"), FI(4, "ahd%03d_f.omd"), FI(5, "ahd%03d_s.omd"),
    FI(35, "ahd%03d_e.md"), FI(36, "ahd%03d_e.tx"), FI(37, "hd%03d.tmx"), FI_END};
struct StPartsFileAtrbInfo s_fiPartsCore[16] = {
    FI(1, "ccr%03d.prm"), FI(2, "ccr%03d.wsc"), FI(3, "acr%03d_n.omd"), FI(4, "acr%03d_f.omd"), FI(5, "acr%03d_s.omd"),
    FI(6, "abo%03d_.mix"), FI(15, "aob%03d_n.omd"), FI(16, "aob%03d_f.omd"), FI(17, "aob%03d_s.omd"),
    FI(18, "aob%03d_.mix"), FI(19, "aeo%03d_n.omd"), FI(20, "aeo%03d_f.omd"), FI(21, "aeo%03d_s.omd"),
    FI(22, "aeo%03d_.mix"), FI(37, "cr%03d.tmx"), FI_END};
struct StPartsFileAtrbInfo s_fiPartsArm[21] = {
    FI(1, "cam%03d.prm"), FI(2, "cam%03d.wsc"), FI(3, "aam%03d_n.omd"), FI(4, "aam%03d_f.omd"), FI(5, "aam%03d_s.omd"),
    FI(7, "acv%03drn.omd"), FI(8, "acv%03drf.omd"), FI(10, "acv%03dr.mix"), FI(11, "acv%03dln.omd"),
    FI(12, "acv%03dlf.omd"), FI(14, "acv%03dl.mix"), FI(23, "aaw%03drn.omd"), FI(24, "aaw%03drf.omd"),
    FI(25, "aaw%03drs.omd"), FI(26, "aaw%03dr.mix"), FI(27, "aaw%03dln.omd"), FI(28, "aaw%03dlf.omd"),
    FI(29, "aaw%03dls.omd"), FI(30, "aaw%03dl.mix"), FI(37, "am%03d.tmx"), FI_END};
struct StPartsFileAtrbInfo s_fiPartsLeg[11] = {
    FI(1, "clg%03d.prm"), FI(2, "clg%03d.wsc"), FI(3, "alg%03d_n.omd"), FI(4, "alg%03d_f.omd"), FI(5, "alg%03d_s.omd"),
    FI(31, "asl%03d_n.omd"), FI(32, "asl%03d_f.omd"), FI(33, "asl%03d_s.omd"), FI(34, "asl%03d_.mix"),
    FI(37, "lg%03d.tmx"), FI_END};
struct StPartsFileAtrbInfo s_fiPartsBooster[7] = {
    FI(1, "cbo%03d.prm"), FI(2, "cbo%03d.wsc"), FI(3, "abo%03d_n.omd"), FI(4, "abo%03d_f.omd"), FI(5, "abo%03d_s.omd"),
    FI(37, "bo%03d.tmx"), FI_END};
struct StPartsFileAtrbInfo s_fiPartsFcs[2] = {FI(1, "cfc%03d.prm"), FI_END};
struct StPartsFileAtrbInfo s_fiPartsGenerator[2] = {FI(1, "cge%03d.prm"), FI_END};
struct StPartsFileAtrbInfo s_fiPartsRadiator[2] = {FI(1, "crd%03d.prm"), FI_END};
struct StPartsFileAtrbInfo s_fiPartsInside[6] = {
    FI(1, "cin%03d.prm"), FI(2, "cin%03d.wsc"), FI(3, "cin%03d.omd"), FI(6, "cin%03d.mix"), FI(39, "cin%03d.tex"),
    FI_END};
struct StPartsFileAtrbInfo s_fiPartsExtension[12] = {
    FI(1, "cex%03d.prm"), FI(2, "cex%03d.wsc"), FI(7, "aex%03drn.omd"), FI(8, "aex%03drf.omd"), FI(9, "aex%03drs.omd"),
    FI(10, "aex%03dr.mix"), FI(11, "aex%03dln.omd"), FI(12, "aex%03dlf.omd"), FI(13, "aex%03dls.omd"),
    FI(14, "aex%03dl.mix"), FI(37, "bo%03d.tmx"), FI_END};
struct StPartsFileAtrbInfo s_fiPartsBackWeapon[12] = {
    FI(1, "cbk%03d.prm"), FI(2, "cbk%03d.wsc"), FI(7, "abk%03drn.omd"), FI(8, "abk%03drf.omd"), FI(9, "abk%03drs.omd"),
    FI(10, "abk%03dr.mix"), FI(11, "abk%03dln.omd"), FI(12, "abk%03dlf.omd"), FI(13, "abk%03dls.omd"),
    FI(14, "abk%03dl.mix"), FI(37, "bk%03d.tmx"), FI_END};
struct StPartsFileAtrbInfo s_fiPartsRightHandWeapon[8] = {
    FI(1, "cgn%03d.prm"), FI(2, "cgn%03d.wsc"), FI(7, "agn%03d_n.omd"), FI(8, "agn%03d_f.omd"), FI(9, "agn%03d_s.omd"),
    FI(10, "agn%03d_.mix"), FI(37, "gn%03d.tmx"), FI_END};
struct StPartsFileAtrbInfo s_fiPartsLeftHandWeapon[8] = {
    FI(1, "csw%03d.prm"), FI(2, "csw%03d.wsc"), FI(11, "asw%03d_n.omd"), FI(12, "asw%03d_f.omd"),
    FI(13, "asw%03d_s.omd"), FI(14, "asw%03d_.mix"), FI(37, "sw%03d.tmx"), FI_END};
struct StPartsFileAtrbInfo s_fiPartsOption[2] = {FI(1, "cop%03d.prm"), FI_END};
#undef FI
#undef FI_END
#endif

struct StPartsFileAtrbInfo *aci_GetPartsFileAtrbInfo(int iKind)
{
    switch ((unsigned int)iKind) {
    case 0: return s_fiPartsHead;
    case 1: return s_fiPartsCore;
    case 2: return s_fiPartsArm;
    case 3: return s_fiPartsLeg;
    case 4: return s_fiPartsBooster;
    case 5: return s_fiPartsFcs;
    case 6: return s_fiPartsGenerator;
    case 7: return s_fiPartsRadiator;
    case 8: return s_fiPartsInside;
    case 9: return s_fiPartsExtension;
    case 10: return s_fiPartsBackWeapon;
    case 11: return s_fiPartsRightHandWeapon;
    case 12: return s_fiPartsLeftHandWeapon;
    case 13: return s_fiPartsOption;
    default: return 0;
    }
}
