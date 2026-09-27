/* shell_data: medal abbreviations (PS 1f7f), emd\shiptext.c (214d), emd\planetxt.c (2164),
 * shell_data_sys.md §6. */
#include "shell_data/sd_priv.h"

/* PS 1f7f:0001 medabbr_open — "medabbr" DS:1058 "dat" DS:1060, stride 15; far handle DS:1054 */
void medabbr_open(void) { ds_far_wr(0x1054, text_load(0x1058, 0x1060, 15)); }

/* PS 1f7f:0021 medabbr_close */
void medabbr_close(void)
{
    text_free(ds_far(0x1054));
    ds_far_wr(0x1054, far_null());
}

/* PS 1f7f:0041 medabbr_text */
FarPtr medabbr_text(s16 i) { return text_get(ds_far(0x1054), i); }

/* PS 214d:0005 shiptxt_open — "shiptxt" DS:1B20 "dat" DS:1B28 */
void shiptxt_open(void)
{
    DSW(PS_DS_shiptxt_slot) = (u16)table_load(0x1B20, 0x1B28, 23, 15, 0x12, 5, far_null(), far_null(), far_null());
    if (!DSW(PS_DS_shiptxt_slot)) sd_fatal(FS_SHIPTEXT, 0x31);
    DSW(PS_DS_shiptxt_n) = (u16)table_count((s16)DSW(PS_DS_shiptxt_slot), 0);
}

/* PS 214d:006f shiptxt_close — the slot variable is not cleared */
void shiptxt_close(void) { table_free((s16)DSW(PS_DS_shiptxt_slot)); }

/* PS 214d:007e shiptxt_name — string pool copy of the 20-char name at +3 */
u16 shiptxt_name(s16 id)
{
    FarPtr r = table_rec((s16)DSW(PS_DS_shiptxt_slot), id);
    if (far_is_null(r)) sd_fatal(FS_SHIPTEXT, 0x41);
    return strpool_copy(far_add(r, 3)).off;
}

/* PS 214d:00d6 shiptxt_is_nation(id, v) */
s16 shiptxt_is_nation(s16 id, s8 v)
{
    FarPtr r = table_rec((s16)DSW(PS_DS_shiptxt_slot), id);
    if (far_is_null(r)) return 0;
    return fc(r, 2) == v;
}

/* PS 214d:0112 shiptxt_count */
s16 shiptxt_count(void) { return (s16)DSW(PS_DS_shiptxt_n); }

/* PS 214d:011c shiptxt_text */
FarPtr shiptxt_text(s16 id)
{
    FarPtr r = table_rec((s16)DSW(PS_DS_shiptxt_slot), id);
    if (far_is_null(r)) sd_fatal(FS_SHIPTEXT, 0x62);
    return table_text((s16)DSW(PS_DS_shiptxt_slot), id, (u32)(s32)fs(r, 0));
}

/* PS 2164:000c pltxt_open — "pltxt" DS:1B2E "dat" DS:1B34, slot DS:1B2C (no failure check) */
void pltxt_open(void)
{
    if (DSW(0x1B2C)) sd_fatal(FS_PLANETXT, 0x11);
    DSW(0x1B2C) = (u16)table_load(0x1B2E, 0x1B34, 2, 15, 0x12, 5, far_null(), far_null(), far_null());
}

/* PS 2164:0065 pltxt_close */
void pltxt_close(void)
{
    table_free((s16)DSW(0x1B2C));
    DSW(0x1B2C) = 0;
}

/* PS 2164:007a pltxt_text */
FarPtr pltxt_text(s16 id)
{
    FarPtr r = table_rec((s16)DSW(0x1B2C), id);
    if (far_is_null(r)) sd_fatal(FS_PLANETXT, 0x2A);
    return table_text((s16)DSW(0x1B2C), id, (u32)(s32)fs(r, 0));
}
