/* shell_ui: file-name helpers ("simshell.c", PS segment 125a) — shell_ui_mission.md §1. */
#include "shell_ui/sui_a_priv.h"

#define VOLUMES_PRESENT 0x0DFF           /* u16: 0 = development set-up, files in per-extension dirs */

/* PS 125a:000f res_add_ext(name, ext) -> path: name + "." + ext (without volumes "<ext>\<name>") */
u16 res_add_ext(u16 name_ds, u16 ext_ds)
{
    u16 r = rtl_strcat(rtl_strcat(name_ds, 0x0E01 /* "." */), ext_ds);
    if (DSW(VOLUMES_PRESENT) != 0) return r;
    return rtl_strcat(rtl_strcat(rtl_strcpy(0x4B94, ext_ds), 0x0E03 /* "\\" */), r);
}

/* PS 125a:0060 dev_chdir(dir) -> ok: only without volumes */
void dev_chdir(u16 dir)
{
    bool ok = true;
    if (DSW(VOLUMES_PRESENT) == 0) ok = rtl_chdir(dir) == 0;
    if (!ok) assert_line(0x4BE2, 0x3E);
}

/* PS 125a:00ab dev_chdir_up */
void dev_chdir_up(void) { dev_chdir(0x0E05 /* ".." */); }

/* PS 125a:00bb namelist_split(p, out) -> n: NUL-terminated words up to one starting with LF */
static s16 namelist_split(FarPtr p, u16 out)
{
    s16 n = 0;
    for (;;) {
        u16 s = strpool_copy(p).off;
        if (DSB(s) == '\n') break;
        n++;
        if (out) {
            ds_far_wr(out, p);
            out = (u16)(out + 4);
        }
        p = far_add(p, (u16)(rtl_strlen(s) + 1));
        if (far_mp(p)[0] == 0) assert_line(0x4BE2, 0x5C);
    }
    return n;
}

/* PS 125a:0139 fread_far(dst, n, f) -> count: byte loop with fgetc (huge pointer) */
static u16 ps_fread_far_125a(FarPtr dst, s32 n, u16 f)
{
    u16 cnt = 0;
    while (n != 0) {
        s16 c = my_fgetc(f);
        if (c == -1) break;
        far_mp(dst)[0] = (u8)c;
        dst = far_norm(far_lin(dst) + 1);
        cnt++;
        n--;
    }
    return cnt;
}

/* PS 125a:01a4 res_size(name) -> file length, 0 if missing */
static s32 res_size(u16 name_ds)
{
    s32 n = 0;
    u16 f = my_fopen(name_ds, 0x0DFC /* "rb" */);
    if (f) {
        n = my_filelength(f);
        my_fclose(f);
    }
    return n;
}

/* PS 125a:01ea namelist_suffix(s, n): "_" after set 1 */
static void namelist_suffix(u16 s, s16 n)
{
    if (n == 1) rtl_strcat(s, 0x0E08 /* "_" */);
}

/* PS 125a:0203 namelist_load(base, mask, total, per_set) -> table of far string pointers */
u16 ps_namelist_load(u16 base_ds, u16 mask, u16 total, u16 per_set)
{
    u16 m = stk_mark();
    u16 name = stk_alloc(14);
    s16 sizes[9];
    rtl_strcpy(name, base_ds);
    u16 end = rtl_strchr(name, 0);
    u16 tbl = lib_realloc((u16)(total << 2));
    if (!tbl) assert_line(0x4BE2, 0xAE);
    rtl_memset(tbl, 0, (u16)(total << 2));
    u16 sum = 0;
    for (s16 n = 0; n < 9; n++) sizes[n] = 0;
    for (s16 n = 0; n < 9; n++) {
        rtl_itoa(n, end, 10);
        namelist_suffix(name, n);
        if (rtl_strlen(name) > 8) assert_line(0x4BE2, 0xBE);
        if (n == 0 || (mask & (1u << ((n - 1) & 0x1F)))) {
            s32 sz = res_size(res_add_ext(name, 0x0DF8 /* "txt" */));
            if (sz == 0) assert_line(0x4BE2, 0xC3);
            sizes[n] = (s16)sz;
            sum = (u16)(sum + (u16)sz);
        }
    }
    FarPtr block = xfarmalloc(sum);
    if (far_is_null(block)) assert_line(0x4BE2, 0xCA);
    u16 slot = tbl;
    for (s16 n = 0; n < 9; n++) {
        s16 room = n == 0 ? (s16)((s16)total - (s16)per_set * 8) : (s16)per_set;
        if (sizes[n] != 0) {
            rtl_itoa(n, end, 10);
            namelist_suffix(name, n);
            u16 f = my_fopen(res_add_ext(name, 0x0DF8), 0x0DFC);
            if (!f) assert_line(0x4BE2, 0xDB);
            ps_fread_far_125a(block, sizes[n], f);
            my_fclose(f);
            if (namelist_split(block, slot) > room) assert_line(0x4BE2, 0xDF);
            block = far_add(block, (u16)sizes[n]);
        }
        slot = (u16)(slot + room * 4);
    }
    stk_release(m);
    return tbl;
}

/* PS 125a:04bb namelist_free(table) */
void namelist_free(u16 table)
{
    xfarfree(ds_far(table));
    lib_free(table);
}
