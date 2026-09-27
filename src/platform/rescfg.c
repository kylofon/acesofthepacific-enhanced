/* INTRO's RESOURCE.CFG line table (platform_resources.md §6.2), segment 05CB: up to 50 lines kept in
 * near strings (DS:25E6, count DS:25E4). Unreferenced by the shipped code paths. INTRO only. */
#include "platform/rtl_priv.h"

#define RC_count 0x25E4
#define RC_lines 0x25E6   /* near char*[50] */

/* INTRO 05cb:000e rcfg_clear */
static void rcfg_clear(void)
{
    for (u16 i = 0; i < 0x32; i++) DSW((u16)(RC_lines + 2 * i)) = 0;
}

/* INTRO 05cb:0027 rcfg_load(fp): fgets 50-byte lines into malloc'ed copies */
void rcfg_load(u16 p0_ds)
{
    u16 m = stk_mark();
    u16 line = stk_alloc(0x3C);
    rcfg_clear();
    DSW(RC_count) = 0;
    while ((s16)DSW(RC_count) < 0x32 && rtl_fgets(line, 0x32, p0_ds)) {
        u16 p = rtl_malloc((u16)(rtl_strlen(line) + 1));
        DSW((u16)(RC_lines + 2 * DSW(RC_count))) = p;
        rtl_strcpy(p, line);
        DSW(RC_count)++;
    }
    if (DSW(RC_count) == 0x32) rtl_fprintf(0x20DC /* stderr */, 0x08C8 /* too many lines */);
    stk_release(m);
}

/* INTRO 05cb:00a6 rcfg_find(key): the first line containing key, or 0 */
u16 rcfg_find(u16 key_ds)
{
    for (s16 i = 0; i < (s16)DSW(RC_count); i++)
        if (rtl_strstr(DSW((u16)(RC_lines + 2 * i)), key_ds)) return DSW((u16)(RC_lines + 2 * i));
    return 0;
}

/* INTRO 05cb:00dc rcfg_free (the entries are not cleared) */
void rcfg_free(void)
{
    for (s16 i = 0; i < (s16)DSW(RC_count); i++) rtl_free(DSW((u16)(RC_lines + 2 * i)));
}
