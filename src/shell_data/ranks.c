/* shell_data: emd\ranks.c (PS 225d) RANKS.DAT, shell_data_sys.md §7. Records DS:52B0 (6 bytes:
 * +0 near name, +2 u8 service, +3 u16 points (unaligned), +5 u8 extra), text DS:52AE, n DS:52B2. */
#include "shell_data/sd_priv.h"

#define REC(r) ((u16)(DSW(PS_DS_rank_tab) + (s16)(r) * 6))

/* PS 225d:0006 ranks_load — "ranks" DS:1BD8 "dat" DS:1BDE "rb" DS:1BE2 */
void ranks_load(void)
{
    u16 f = my_fopen(make_name(0x1BD8, -1, 0x1BDE), 0x1BE2);
    if (!f) sd_fatal(FS_RANKS, 0x2F);
    my_fread(PS_DS_rank_n, 2, 1, f);
    DSW(PS_DS_rank_tab) = xmalloc((u16)(DSW(PS_DS_rank_n) * 6));
    my_fread(DSW(PS_DS_rank_tab), 6, (s16)DSW(PS_DS_rank_n), f);
    u16 m = stk_mark();
    u16 len = stk_alloc(2);
    my_fread(len, 2, 1, f);
    DSW(PS_DS_rank_text) = xmalloc(DSW(len));
    my_fread(DSW(PS_DS_rank_text), 1, (s16)DSW(len), f);
    stk_release(m);
    my_fclose(f);
    for (s16 i = 0; i < (s16)DSW(PS_DS_rank_n); i++)
        DSW(REC(i)) = (u16)(DSW(REC(i)) + DSW(PS_DS_rank_text));
}

/* PS 225d:00f7 rank_name (uncalled) */
u16 rank_name(s8 r) { return DSW(REC(r)); }

/* PS 225d:010f rank_service */
u8 rank_service(s8 r) { return DSB(REC(r) + 2); }

/* PS 225d:0128 rank_points */
u16 rank_points(s8 r) { return DSW(REC(r) + 3); }

/* PS 225d:0141 rank_extra */
u8 rank_extra(s8 r) { return DSB(REC(r) + 5); }

/* PS 225d:015a rank_first(svc) — n if none */
s8 rank_first(s8 svc)
{
    s8 r = 0;
    while ((s16)r < (s16)DSW(PS_DS_rank_n) && DSC(REC(r) + 2) != svc) r++;
    return r;
}

/* PS 225d:018d rank_last(svc) — the top rank of the service */
s8 rank_last(s8 svc)
{
    s8 r = 0;
    while ((s16)r < (s16)DSW(PS_DS_rank_n) && DSC(REC(r) + 2) != svc) r++;
    while ((s16)r < (s16)DSW(PS_DS_rank_n) && DSC(REC(r) + 2) == svc) r++;
    return (s8)(r - 1);
}

/* PS 225d:01de ranks_free */
void ranks_free(void)
{
    xfree(DSW(PS_DS_rank_tab));
    xfree(DSW(PS_DS_rank_text));
}
