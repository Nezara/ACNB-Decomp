/* src/stage/dmylight.cpp - stage dummy markers: Stage_Begin runs a per-type initialiser over the 128 markers; only
 * type 8 has one, which places a map point light. */

static void make_plight(union_faa1d38d *dmy)
{
    int frm = dmy->plit.frm;
    if (!(frm > 0)) frm = 2;
    DF_PutPointLight(dmy->plit.kind, dmy->plit.pltype, dmy->plit.loc, dmy->plit.range, frm, dmy->plit.col_r,
                     dmy->plit.col_g, dmy->plit.col_b);
}

#ifdef ACNB_SOURCEPORT
void (*init_func_tab[16])(union_faa1d38d *) = {0, 0, 0, 0, 0, 0, 0, 0, make_plight, 0, 0, 0, 0, 0, 0, 0};
#endif

/* (the signed marker type indexes the table unchecked) */
void STAGE_InitDummy(ArrayFileSt *ap)
{
    PS2_FRAME(0x30);
    MarkerSt *dmy = ap->marker;
    for (int ii = 0; ii < 0x80; ii++, dmy++) {
        void (*fp)(union_faa1d38d *) = init_func_tab[(int)dmy->type];
        if (fp) gcall(fp, (union_faa1d38d *)dmy);
    }
}
