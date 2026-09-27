/* sim_flow: small resident helpers — simshell.c resource names and add-on modules (55a4), ovl.c
 * (519d), free disk space (36ee), the empty debug hook (4b9e:0005), the dead named-id list (5b71).
 * sim_flow.md §8, sim_flow_extra.md. */
#include "sim_flow/sfa_priv.h"

#define ASSERT_OVL_C       far_make(SEG(0x3882), 0)
#define ASSERT_SIMSHELL_C  far_make(SEG(0x38A0), 0)
#define VOLUMES_ON         DSW(0x06F8)

/* AP 519d:0000 file_load(buf, size, count, name) — the whole file or fatal */
void file_load(u16 buf_ds, s16 size, s16 count, u16 name_ds)
{
    u16 f = my_fopen(name_ds, 0x05EB /* "rb" */);
    if (!f) assert_fail(ASSERT_OVL_C, 0x29, name_ds);
    s16 n = (s16)(count * size);
    if (my_fread(buf_ds, 1, n, f) != n) assert_fail(ASSERT_OVL_C, 0x2B, name_ds);
    my_fclose(f);
}

/* AP 519d:008a (named ovl_init_nop in symbols.csv; not empty) — SHADES1.DAT, 256 bytes -> DS:4092 */
void ovl_init_nop(void)
{
    file_load(0x4092, 1, 0x100, res_name(0x0624 /* "shades1" */, 0x05BC /* "DAT" */));
}

/* AP 55a4:0000 res_name(name, ext) — "name.ext"; without volumes "<ext>\name.ext" in DS:44CA */
u16 res_name(u16 name, u16 ext)
{
    u16 n = make_filename(name, ext);
    if (VOLUMES_ON) return n;
    rtl_strcpy(0x44CA, ext);
    rtl_strcat(0x44CA, 0x0704 /* "\\" */);
    rtl_strcat(0x44CA, n);
    return 0x44CA;
}

/* AP 55a4:007f res_chdir(dir) — unless volumes: chdir(dir) == 0 */
s16 res_chdir(u16 dir)
{
    if (VOLUMES_ON) return 1;
    return rtl_chdir(dir) == 0;
}

/* AP 55a4:00a2 */
void res_chdir_up(void)
{
    res_chdir(0x0706 /* ".." */);
}

/* AP 55a4:00b1 res_chain(far p, far **list) — NUL-separated strings up to a line starting '\n' */
s16 res_chain(FarPtr p, u16 list)
{
    s16 n = 0;
    for (;;) {
        u16 s = far_to_near_str(p);
        if (DSB(s) == 0x0A) return n;
        n++;
        if (list) {
            ds_far_wr(list, p);
            list = (u16)(list + 4);
        }
        p = far_add(p, (u16)(rtl_strlen(s) + 1));
    }
}

/* AP 55a4:0108 fread_far(far dst, long n, f) -> bytes read (byte-wise fgetc into a huge pointer) */
s32 ap_fread_far_55a4(FarPtr dst, s32 n, u16 f)
{
    s32 cnt = 0;
    u32 lin = far_lin(dst);
    while (n-- != 0) {
        s16 c = my_fgetc(f);
        if (c == -1) break;
        mem[lin++] = (u8)c;
        cnt++;
    }
    return cnt;
}

/* AP 55a4:0167 res_exists(name) -> the file length (0 when missing) */
s32 res_exists(u16 name)
{
    s32 len = 0;
    u16 f = my_fopen(name, 0x05EB /* "rb" */);
    if (f) {
        len = my_filelength(f);
        my_fclose(f);
    }
    return len;
}

/* AP 55a4:01b5 res_patch_module(s, n) — n > 0 and add-on module n present: append DS:0709 */
void res_patch_module(u16 s, s16 n)
{
    if (n-- == 0) return;
    if (modules_mask() & (1 << (n & 31))) rtl_strcat(s, 0x0709);
}

/* AP 55a4:01f0 res_load_set(name, mask, n, k) -> table of n far pointers: the NUL-separated name
 * lists <name><i>.TXT (i = 0..8; i > 0 only for the add-on modules in mask), read into one far block */
u16 res_load_set(u16 name, u16 mask, u16 n, u16 k)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x0C), sizes = stk_alloc(0x12);
    rtl_strcpy(buf, name);
    u16 end = (u16)(buf + rtl_strlen(buf));
    u16 table = mem_new((u16)(n << 2), 0, far_to_near_str(ASSERT_SIMSHELL_C), 0xAE);
    memset(DSP(table), 0, (size_t)(u16)(n << 2));
    s16 total = 0;
    memset(DSP(sizes), 0, 0x12);
    u8 bit = 0xFF;
    for (s16 si = 0; bit != 8; si++, bit++) {
        rtl_itoa(si, end, 10);
        res_patch_module(buf, si);
        if (si == 0 || (mask & (u16)(1 << (u8)bit))) {
            s16 sz = (s16)res_exists(res_name(buf, 0x05C4 /* "TXT" */));
            DSW((u16)(sizes + 2 * si)) = (u16)sz;
            total = (s16)(total + sz);
        }
    }
    FarPtr data = mem_far_new((u32)(s32)total, 0);
    u16 cur = table;
    for (s16 si = 0; si < 9; si++) {
        s16 cnt = si ? (s16)k : (s16)(n - (k << 3));
        s16 sz = DSS((u16)(sizes + 2 * si));
        if (sz) {
            rtl_itoa(si, end, 10);
            res_patch_module(buf, si);
            u16 f = my_fopen(res_name(buf, 0x05C4), 0x05EB);
            ap_fread_far_55a4(data, (s32)sz, f);
            my_fclose(f);
            res_chain(data, cur);
            data = far_add(data, (u16)sz);
        }
        cur = (u16)(cur + (cnt << 2));
    }
    stk_release(m);
    return table;
}

/* AP 55a4:03a7 res_free(table) — the far block (first entry) and the table */
void res_free(u16 p)
{
    mem_far_delete(ds_far(p));
    mem_delete(p, 0, far_to_near_str(ASSERT_SIMSHELL_C), 0xEB);
}

/* AP 55a4:03d9 modules_detect — add-on volumes M1SHL.DYN .. M8SHL.DYN */
void ap_modules_detect(void)
{
    u16 i = 0;
    DSB(0x44E8) = 0;
    for (u8 c = '1'; c != '9'; c++, i++) {
        DSB(0x06FB) = c;
        u16 f = rtl_fopen(0x06FA /* "M#SHL.DYN" */, 0x070B /* "rb" */);
        if (f) {
            rtl_fclose(f);
            DSB(0x44E8) |= (u8)(1 << (i & 7));
        }
    }
}

/* AP 55a4:0429 */
u8 modules_mask(void)
{
    return DSB(0x44E8);
}

/* AP 36ee:0003 disk_free(drive) -> free bytes, -1 on error */
s32 disk_free(s16 drive)
{
    if (drive == 0) {
        u8 dl = (u8)(DSB(0x4032) & 0xEF);
        if ((s8)dl < 'A' || (s8)dl > 'Z') drive = (s16)(rtl_getdisk() + 1);
        else drive = (s16)((s8)dl - 0x40);
    }
    u16 m = stk_mark();
    u16 df = stk_alloc(8);
    rtl_getdfree((u8)drive, df);
    s32 r;
    if (DSW(df + 6) == 0xFFFF) r = -1;
    else r = (s32)((u32)DSW(df) * DSW(df + 4) * DSW(df + 6));
    stk_release(m);
    return r;
}

/* AP 4b9e:0005 — empty debug hook */
void debug_hook1(void) {}

/* ---- 5b71: named-id list (dead code in AP 1.2) -------------------------------------------------- */

/* AP 5b71:00cd namelist_find(id, name, head) */
u16 namelist_find(s16 id, u16 name, u16 head)
{
    for (u16 n = head; n; n = DSW(n)) {
        if (id && DSS(n + 2) == id) return n;
        if (name && rtl__fstricmp(ds_ptr((u16)(n + 4)), ds_ptr(name)) == 0) return n;
    }
    return 0;
}

/* AP 5b71:0000 namelist_add(id, name, head) -> id */
s16 namelist_add(s16 id, u16 name, u16 head)
{
    u16 f = namelist_find(0, name, head);
    if (f && id) {
        DSW(f + 2) = (u16)id;
        return id;
    }
    if (f) id = DSS(f + 2);
    if (id == 0 && !(f && DSS(f + 2) == id)) {
        id = 1;
        u16 si = head;
        /* PORT: the original walks from the head and, with an empty list, reads DS:0000 as a node */
        while (DSW(si) != 0) {
            if (DSS(si + 2) >= id) id = (s16)(DSS(si + 2) + 1);
            si = DSW(si);
        }
        if (DSS(si + 2) >= id) id = (s16)(DSS(si + 2) + 1);
    }
    if (f) {
        DSW(f + 2) = (u16)id;
        return id;
    }
    u16 d = DSW(0x4DE4);
    if (d) DSW(0x4DE4) = DSW(d);
    else d = arena_alloc(0x19);
    DSW(d + 2) = (u16)id;
    DSW(d) = 0;
    rtl__fstrncpy(ds_ptr((u16)(d + 4)), ds_ptr(name), 0x15);
    DSB(d + 0x18) = 0;
    u16 si = head;
    while (DSW(si) != 0) si = DSW(si);
    DSW(si) = d;
    return id;
}

/* AP 5b71:0118 namelist_load(f, path, head) -> count */
s16 ap_namelist_load(u16 f, u16 path, u16 head)
{
    if (!f || !path || !head) return 0;
    s16 was_open = chunk_is_open(f);
    if (!was_open) f = chunk_open(f);
    if (!f) return 0;
    if (chunk_find(f, path, 0) == -1 && chunk_find(f, path, -1) == -1) return 0;
    s16 n = (s16)fread_word(f);
    s16 cnt = n;
    while (cnt-- != 0) {
        s16 id = (s16)fread_word(f);
        u16 s = fread_str(f);
        if (s) namelist_add(id, s, head);
    }
    if (!was_open) chunk_close(f);
    return n;
}
