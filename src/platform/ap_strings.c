/* AP string helpers 368f (video_misc.md §3). */
#include "platform/rtl_priv.h"

/* AP 368f:000e memswap(a, b, n) (no callers) */
void memswap(u16 a, u16 b, s16 n)
{
    while (--n >= 0) {
        u8 t = DSB(a);
        DSB(a++) = DSB(b);
        DSB(b++) = t;
    }
}

/* AP 368f:002f memswap_huge(a, b, n) (no callers) */
void memswap_huge(FarPtr a, FarPtr b, s16 n)
{
    while (--n >= 0) {
        u8 t = rd8(a.seg, a.off);
        wr8(a.seg, a.off, rd8(b.seg, b.off));
        a = far_norm(far_lin(a) + 1);                 /* PADA@ */
        wr8(b.seg, b.off, t);
        b = far_norm(far_lin(b) + 1);
    }
}

/* AP 368f:020a str_subst(s, c, r): the first c among the strlen(s)+1 bytes becomes r */
u16 str_subst(u16 s_ds, s8 c, s8 r)
{
    u16 n = (u16)(ds_strlen(s_ds) + 1);
    for (u16 i = 0; i < n; i++)
        if (DSC((u16)(s_ds + i)) == c) { DSC((u16)(s_ds + i)) = r; break; }
    return s_ds;
}

/* AP 368f:0243 strip_ext_all(v, n) (no callers) */
void strip_ext_all(u16 v_ds, s16 n)
{
    while (--n >= 0) { str_subst(DSW(v_ds), '.', 0); v_ds = (u16)(v_ds + 2); }
}

/* AP 368f:0268 strlist_remove(v, s, n) -> n (no callers) */
s16 strlist_remove(u16 v_ds, u16 s_ds, s16 n)
{
    s16 i = 0;
    for (; i < n; i++) {
        u16 e = DSW((u16)(v_ds + 2 * i));
        u16 k = (u16)(ds_strlen(s_ds) + 1), j = 0;
        u8 x, y;
        do { x = DSB((u16)(e + j)); y = DSB((u16)(s_ds + j)); j++; } while (--k && x == y);
        if (x == y) break;
    }
    if (i < n) {
        n--;
        if (n > i) rtl_memmove((u16)(v_ds + 2 * i), (u16)(v_ds + 2 * i + 2), (u16)(2 * (n - i)));
    }
    return n;
}

/* AP 368f:02e6 make_filename(name, ext) -> DS:712C: name without extension + "." + ext */
u16 make_filename(u16 name_ds, u16 ext_ds)
{
    rtl_strcpy(0x712C, name_ds);
    str_subst(0x712C, '.', 0);
    rtl_strcat(0x712C, 0x3723 /* "." */);
    rtl_strcat(0x712C, ext_ds);
    return 0x712C;
}

/* AP 368f:035c make_filename_dev(name, ext): without a DAC-capable display (DS:26DF = 0) the name
 * becomes "E" + name[0..7) (EGA variant files) */
u16 make_filename_dev(u16 name_ds, u16 ext_ds)
{
    if (!DSB(0x26DF)) {
        rtl_strcpy(0x3719, name_ds);
        DSB(0x3720) = 0;
        name_ds = 0x3718;
    }
    return make_filename(name_ds, ext_ds);
}

/* AP 368f:03a1 itoa_static(v) -> DS:714A */
u16 itoa_static(s16 v) { return rtl_itoa(v, 0x714A, 10); }
