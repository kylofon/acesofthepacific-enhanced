/* shell_data: shell\fltplan.c (PS 1155) flight-plan messages from FLTPLAN.DAT, shell_data.md §10.
 * Globals: DS:4B40 waypoint number (%d), DS:4B42 message count, DS:4B44 string group, DS:4B45 far carrier
 * prefix, DS:4B49 far ptr[] messages, DS:0B50 built flag, DS:0B51 sim variant flag; DS:419A scratch. */
#include "shell_data/sd_priv.h"
#include "shell_ui/shell_ui.h"

#define FP_WPN   0x4B40
#define FP_N     0x4B42
#define FP_GROUP 0x4B44
#define FP_PFX   0x4B45
#define FP_MSGS  PS_DS_fltplan_msgs
#define FP_BUF   0x419A

/* PS 1155:0002 fltplan_objective_name(kind, idx) — place kind string / convoy name string */
static FarPtr fltplan_objective_name(s16 kind, s16 idx)
{
    if (kind == 1) return strings_get(0x16, (s16)DSC(PLACE(idx) + 1));
    return strings_get(0x17, (s16)DSC(CONV(idx) + 2));
}

/* PS 1155:0042 fltplan_code(c) — the text of %a..%g */
static FarPtr fltplan_code(s8 c)
{
    const u16 s0 = FLT(0, 0), s1 = FLT(1, 0), s2 = FLT(2, 0);
    switch (c - 'a') {
    case 0: return fltplan_objective_name((s16)DSC(s0 + 8), (s16)DSW(s0 + 0x20));
    case 1: {
        u16 rec = DSB(s0 + 6) ? DSW(CONV(DSW(s0 + 0x1E)) + 0x0C) : DSW(PLACE(DSW(s0 + 0x1E)) + 4);
        FarPtr name = station_name(station_find(rec, DSB(MAREA), DSC(s0 + 6)));
        FarPtr pfx = DSB(s0 + 6) ? ds_far(FP_PFX) : ds_ptr(0x0B59);
        rtl_sprintf(FP_BUF, 0x0B52, (unsigned)pfx.off, (unsigned)pfx.seg, (unsigned)name.off, (unsigned)name.seg);
        return ds_ptr(FP_BUF);
    }
    case 2:
        strcpy_far_src(FP_BUF, ac_plural((s16)DSW(s2 + 0x18)));
        return ds_ptr(FP_BUF);
    case 3:
        return ds_ptr(rtl_itoa((s16)DSW(FP_WPN), FP_BUF, 10));
    case 4:
        return ace_name((s16)DSW(s1 + 0x0E));
    case 5: {
        FarPtr n = ac_name((s16)DSW(s1 + 0x18));
        rtl_sprintf(FP_BUF, 0x0B5A, (unsigned)n.off, (unsigned)n.seg);
        return ds_ptr(FP_BUF);
    }
    case 6: {
        u16 b = fm_bearing(DSW(s0 + 0x22), DSW(s1 + 0x22));
        return ds_ptr(rtl_ltoa((s32)b, FP_BUF, 10));
    }
    }
    return ds_ptr(0x0B59);   /* PORT: the original returns an uninitialised pointer for other letters */
}

/* PS 1155:01a5 fltplan_expand(dst, src) — %x replaced by fltplan_code(x) */
static FarPtr fltplan_expand(FarPtr dst, FarPtr src)
{
    FarPtr d = dst;
    for (; fb(src, 0); src.off++) {
        if (fb(src, 0) == '%') {
            src.off++;
            FarPtr t = fltplan_code(fc(src, 0));
            for (; fb(t, 0); t.off++, d.off++) fwb(d, 0, fb(t, 0));
        } else {
            fwb(d, 0, fb(src, 0));
            d.off++;
        }
    }
    fwb(d, 0, 0);
    return dst;
}

/* PS 1155:0230 fltplan_add(group, k, i) — message i = expand(str(group, k)) as a far copy */
static void fltplan_add(s8 g, s16 k, s16 i)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x1F4);
    fltplan_expand(ds_ptr(buf), strings_get((s16)g, k));
    u16 n = (u16)(rtl__fstrlen(ds_ptr(buf)) + 1);
    FarPtr p = xfarmalloc((u32)(s32)(s16)n);
    far_wr(cur_dgroup, (u16)(FP_MSGS + i * 4), p);
    fmemcpy(p, ds_ptr(buf), n);
    stk_release(m);
}

/* PS 1155:02a3 fltplan_build — "fltplan" DS:0B5F "dat" DS:0B67 */
void fltplan_build(void)
{
    DSW(FP_N) = DSW(0x62E1);
    DSB(FP_GROUP) = (u8)(DSB(MTYPE) + (DSB(0x0B51) ? 11 : 0));
    ps_strings_load_24e3(make_name(0x0B5F, -1, 0x0B67));
    ds_far_wr(FP_PFX, strings_get(0x18, 0));
    fltplan_add(DSC(FP_GROUP), 0, 0);
    s16 r = (s16)DSC(FP_GROUP) % 11;
    if (r == 2 || r == 3) {
        for (DSW(FP_WPN) = 1; (s16)(DSW(FP_N) - 1) > (s16)DSW(FP_WPN); DSW(FP_WPN)++)
            fltplan_add(DSC(FP_GROUP), 1, (s16)DSW(FP_WPN));
        fltplan_add(DSC(FP_GROUP), 2, (s16)DSW(FP_WPN));
    } else {
        for (DSW(FP_WPN) = 1; (s16)DSW(FP_WPN) < (s16)DSW(FP_N); DSW(FP_WPN)++)
            fltplan_add(DSC(FP_GROUP), (s16)DSW(FP_WPN), (s16)DSW(FP_WPN));
    }
    strings_free();
    DSB(0x0B50) = 1;
}

/* PS 1155:0398 fltplan_messages */
u16 fltplan_messages(void) { return FP_MSGS; }

/* PS 1155:03a2 fltplan_free */
void fltplan_free(void)
{
    for (s16 i = 0; i < (s16)DSW(FP_N); i++) xfarfree(far_rd(cur_dgroup, (u16)(FP_MSGS + i * 4)));
    DSB(0x0B50) = 0;
}

/* PS 1155:03d2 fltplan_sim_on */
void fltplan_sim_on(void) { DSB(0x0B51) = 1; }

/* PS 1155:03dc fltplan_sim_off */
void fltplan_sim_off(void) { DSB(0x0B51) = 0; }
