/* shell_data: sys\helper.c (PS 22ce) — allocators, wipe, string helpers, shell_data_sys.md §2.
 * (The page-pair setters 22ce:004a..00fb are video/ps_pages.c.) */
#include "shell_data/sd_priv.h"
#include "video/video.h"
#include "host.h"

/* PS 22ce:000d timer_countdown — DS:2E3F (decremented by timer_isr) */
s16 timer_countdown(void) { return (s16)LW(G_timer_countdown); }

/* PS 22ce:0017 assert_index (uncalled) */
s16 assert_index(s8 i, s8 n)
{
    if (i < 0 || i >= n) sd_fatal(FS_HELPER, 0x42);
    return 1;
}

/* PS 22ce:0110 clip_full */
void ps_clip_full(void)
{
    DSB(0x230C) = 1;
    DSB(0x2303) = 1;
    DSW(0x2304) = 0;
    DSW(0x2308) = 0;
    DSW(0x2306) = 319;
    DSW(0x230A) = 199;
}

/* PS 22ce:0137 wipe_rect — venetian-blind reveal, 10 passes of 5 PIT ticks */
void wipe_rect(s16 x, s16 y, s16 w, s16 h)
{
    s16 bottom = (s16)(y + h - 1);
    for (s16 pass = 0; pass < 10; pass++) {
        LW(G_timer_countdown) = 5;
        for (s16 d = 0; (s16)(y + pass + d) <= bottom; d = (s16)(d + 10))
            if (w) gfx_copy_rect(x, (s16)(y + pass + d), w, 1);
        while (timer_countdown()) host_pump();
    }
}

/* PS 22ce:019c xmalloc — fatal on 0 or out of memory; malloc = 0000:03c1 */
u16 xmalloc(u16 n)
{
    if (n == 0) sd_fatal(FS_HELPER, 0xCE);
    u16 p = lib_realloc(n);
    if (!p) sd_fatal(FS_HELPER, 0xD0);
    return p;
}

/* PS 22ce:01ff memzero */
u16 memzero(u16 p_ds, u16 n)
{
    rtl_memset(p_ds, 0, n);
    return p_ds;
}

/* PS 22ce:021c xfree — free = 0000:0454 */
void xfree(u16 p_ds) { lib_free(p_ds); }

/* PS 22ce:022a xfarmalloc */
FarPtr xfarmalloc(u32 n) { return far_alloc(n); }

/* PS 22ce:023e fmemset (uncalled) */
void fmemset(FarPtr d, s8 c, u16 n)
{
    for (u16 i = 0; i < n; i++) { fwb(d, 0, (u8)c); d.off++; }
}

/* PS 22ce:026e fmemcpy — byte loop, forward */
void fmemcpy(FarPtr d, FarPtr s, u16 n)
{
    for (u16 i = 0; i < n; i++) { fwb(d, 0, fb(s, 0)); d.off++; s.off++; }
}

/* PS 22ce:02b0 xfarfree */
void xfarfree(FarPtr p) { far_free(p); }

/* PS 22ce:02c2 str_ordinal (uncalled in PS) */
u16 str_ordinal(u16 s_ds, s16 n)
{
    s8 d = (s8)(n % 10);
    s8 t = (s8)((n - d) % 100);
    u16 suf;
    if (d < 1 || d > 3 || t == 10) suf = 0x1CD5;          /* "th" */
    else suf = d == 1 ? 0x1CCC : d == 2 ? 0x1CCF : 0x1CD2; /* "st" "nd" "rd" */
    rtl_strcat(s_ds, suf);
    return s_ds;
}

/* PS 22ce:032d str_int */
u16 str_int(u16 s_ds, s16 n) { return rtl_itoa(n, s_ds, 10); }

/* PS 22ce:0346 str_cat_int (uncalled) */
u16 str_cat_int(u16 s_ds, s16 n) { return str_int((u16)(s_ds + rtl_strlen(s_ds)), n); }

/* PS 22ce:0367 str_upper */
u16 str_upper(u16 s_ds)
{
    for (u16 p = s_ds; DSB(p); p++) DSB(p) = (u8)rtl_toupper((s16)DSC(p));
    return s_ds;
}

/* PS 22ce:038d str_lower (uncalled) */
u16 str_lower(u16 s_ds)
{
    for (u16 p = s_ds; DSB(p); p++) DSB(p) = (u8)rtl_tolower((s16)DSC(p));
    return s_ds;
}

/* PS 22ce:03b3 str_capitalize (uncalled) */
u16 str_capitalize(u16 s_ds)
{
    int up = 1;
    for (u16 p = s_ds; DSB(p); p++) {
        s16 c = up ? rtl_toupper((s16)DSC(p)) : rtl_tolower((s16)DSC(p));
        DSB(p) = (u8)c;
        up = ((u8)c == ' ' || (u8)c == '-');
    }
    return s_ds;
}
