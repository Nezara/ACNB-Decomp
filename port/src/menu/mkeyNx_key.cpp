/* src/menu/mkeyNx_key.cpp - the key-config data layer (OLMENU): the player's key type A / B and button assignment in
 * g_gamedata[player].option.keyconf, plus the per-type action tables the key-config pages edit.
 *
 * s98: only the two key-type accessors are native (the system-option new-game dialog's SetKeyType is reached in the
 * test room replay garage3). MenuKeyCnfg_SetKeyAssignData / GetKeyAssignData / CheckKeyAssignData and the static
 * __MenuKeyCnfg_SetKeyActData_* / __ManuKeyCnfg_ApplyGameKeyConf stay interpreted (unreached, out of s87 scope). */
/* the mode byte is stored as a 0 / 1 flag: any non-zero type (the low byte of nType) is B */
void MenuKeyCnfg_SetKeyType(_KEYCNFG_KEY_TYPE nType, int nPlayer)
{
    g_gamedata[nPlayer].option.keyconf.mode = (unsigned char)nType != 0;
}

void MenuKeyCnfg_GetKeyType(_KEYCNFG_KEY_TYPE *pType, int nPlayer)
{
    *pType = g_gamedata[nPlayer].option.keyconf.mode != 0;
}
