/* lib/layout.cpp — reader for the menu / HUD 2D layout files. The layouts are bundled in a binder (s_LayoutData);
 * a layout file holds a scene header at +0x30 (unit count @+0x20), named 0x40-byte units from +0x70 (parts-list
 * index, world offset) and, at the offset stored in word 1, a block of parts lists: a count, then lists of a 0x20-byte
 * header (parts count, CommonID[3]) followed by that many 0x40-byte part records (id, rectangle, angle, texture,
 * priority, UV, colour, blend). DF_LO_GetLayoutData resolves (unit name, part id) to a _LAYOUT_RET_DATA. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include <string.h>

/* Binary search of the binder's file table (sorted by id) */
void *LAYOUT_GetLayoutData(int file_id)
{
    StFileInfo *pFileInfo = s_LayoutData.BinderMng.file_infop;
    int low = 0;
    int high = (int)s_LayoutData.BinderMng.head->file_num - 1;
    while (low <= high) {
        int mid = (low + high) / 2;
        StFileInfo *p = &pFileInfo[mid];
        int id = p->file_id;
        if (file_id < id) high = mid - 1;
        else if (id < file_id) low = mid + 1;
        else return s_LayoutData.pBindData + p->data_offset;
    }
    return NULL;
}

char LAYOUT_RegistBindData(void *pFileImage)
{
    PS2_FRAME(0x20);
    Binder_Init(&s_LayoutData.BinderMng, pFileImage, 0);
    s_LayoutData.pBindData = (char *)pFileImage;
    return 1;
}

/* Parts count of the parts list used by the first unit named pInName (0 if that list does not exist). The unit
 * count is re-read every iteration. */
int DF_LO_GetPartsNumInUnitFromName(unsigned char *pInSrc, unsigned char *pInName)
{
    PS2_FRAME(0x70);
    struct_a8f88fa8 *pLOScenePrim = (struct_a8f88fa8 *)(pInSrc + 0x30);
    struct_73310964 *pLOScenePartsUnit = (struct_73310964 *)(pLOScenePrim + 1);
    struct_b713691c *pLOPartsList = (struct_b713691c *)(pInSrc + *(unsigned int *)(pInSrc + 4));
    struct_7ced33ba *pLOPartsListPrim = (struct_7ced33ba *)(pLOPartsList + 1);
    for (int SPartsNum = 0; SPartsNum < pLOScenePrim->PartsNum; SPartsNum++, pLOScenePartsUnit++) {
        if (strcmp((const char *)pLOScenePartsUnit->Name, (const char *)pInName) != 0) continue;
        int num = pLOPartsList->Num;
        for (int PartsNum = 0; PartsNum < num; PartsNum++) {
            if (PartsNum == pLOScenePartsUnit->UsePartsUnitIndex) return pLOPartsListPrim->PartsNum;
            pLOPartsListPrim = (struct_7ced33ba *)((char *)pLOPartsListPrim + 0x20 + (pLOPartsListPrim->PartsNum << 6));
        }
        return 0;
    }
    return 0;
}

/* Copies the fields InFlag selects from part InPartsID of parts list InPartsListIndex; CommonID is always copied.
 * Returns 0 if the list or the part does not exist. */
int DF_LO_GetLocalData(unsigned char *pInSrc, int InPartsListIndex, int InPartsID, _LAYOUT_RET_DATA *pOutData, unsigned int InFlag)
{
    PS2_FRAME(0x50);
    struct_b713691c *pLOPartsList = (struct_b713691c *)(pInSrc + *(unsigned int *)(pInSrc + 4));
    struct_7ced33ba *pLOPartsListPrim = (struct_7ced33ba *)(pLOPartsList + 1);
    int PrimNum = pLOPartsList->Num;
    for (int i = 0; i < PrimNum; i++) {
        if (i != InPartsListIndex) {
            pLOPartsListPrim = (struct_7ced33ba *)((char *)pLOPartsListPrim + 0x20 + (pLOPartsListPrim->PartsNum << 6));
            continue;
        }
        struct_8cd505c0 *pLOPartsListParts = (struct_8cd505c0 *)(pLOPartsListPrim + 1);
        int PartsNum = pLOPartsListPrim->PartsNum;
        for (int j = 0; j < PartsNum; j++, pLOPartsListParts++) {
            if (pLOPartsListParts->PerID != InPartsID) continue;
            if (InFlag & 1) {
                pOutData->Pos.x = pLOPartsListParts->LocalPos[0];
                pOutData->Pos.y = pLOPartsListParts->LocalPos[1];
                pOutData->Pos.w = pLOPartsListParts->LocalPos[2] - pLOPartsListParts->LocalPos[0];
                pOutData->Pos.h = pLOPartsListParts->LocalPos[3] - pLOPartsListParts->LocalPos[1];
            }
            if (InFlag & 2) {
                for (int k = 0; k < 4; k++) pOutData->UV[k] = pLOPartsListParts->ClipCoord[k];
            }
            if (InFlag & 4) {
                unsigned char *col = (unsigned char *)&pOutData->Col;
                for (int k = 0; k < 4; k++) col[k] = pLOPartsListParts->Col[k];
            }
            if (InFlag & 8) pOutData->TexID = pLOPartsListParts->TextureNameID;
            if (InFlag & 0x10) memcpy(pOutData->Blend, pLOPartsListParts->Blend, 5);
            if (InFlag & 0x40) pOutData->Angle = pLOPartsListParts->Angle;
            if (InFlag & 0x80) pOutData->DrawPriority = pLOPartsListParts->DrawPriority;
            for (int k = 0; k < 3; k++) pOutData->CommonID[k] = pLOPartsListPrim->CommonID[k];
            return 1;
        }
        return 0;
    }
    return 0;
}

/* Part InGetID of the first unit named pInName, positioned by the unit's world offset. The part is fetched into a
 * local at the original stack slot (sp+0x80) with flag 0xdf; a matching unit without the part returns 0. */
int DF_LO_GetLayoutData(unsigned char *pInSrc, unsigned char *pInName, int InGetID, _LAYOUT_RET_DATA *pOutData)
{
    PS2_FRAME(0xd0);
    PS2_STACK(_LAYOUT_RET_DATA, RetData, 0x80);
    struct_a8f88fa8 *pLOScenePrim = (struct_a8f88fa8 *)(pInSrc + 0x30);
    struct_73310964 *pLOScenePartsUnit = (struct_73310964 *)(pLOScenePrim + 1);
    for (int SPartsNum = 0; SPartsNum < pLOScenePrim->PartsNum; SPartsNum++, pLOScenePartsUnit++) {
        if (strcmp((const char *)pLOScenePartsUnit->Name, (const char *)pInName) != 0) continue;
        int xofs = pLOScenePartsUnit->WorldPos[0];
        int yofs = pLOScenePartsUnit->WorldPos[1];
        if (DF_LO_GetLocalData(pInSrc, pLOScenePartsUnit->UsePartsUnitIndex, InGetID, &RetData, 0xdf) == 0) return 0;
        pOutData->Pos.x = RetData.Pos.x + xofs;
        pOutData->Pos.y = RetData.Pos.y + yofs;
        pOutData->Pos.w = RetData.Pos.w;
        pOutData->Pos.h = RetData.Pos.h;
        for (int k = 0; k < 4; k++) pOutData->UV[k] = RetData.UV[k];
        pOutData->Col = RetData.Col;
        pOutData->TexID = RetData.TexID;
        pOutData->Angle = RetData.Angle;
        pOutData->DrawPriority = RetData.DrawPriority;
        for (int k = 0; k < 3; k++) pOutData->CommonID[k] = RetData.CommonID[k];
        memcpy(pOutData->Blend, RetData.Blend, 5);
        return 1;
    }
    return 0;
}
