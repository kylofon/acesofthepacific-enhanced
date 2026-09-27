/* shell_data: memory stream (PS 1efe) and sys\snario.c (1f06) — the mission file reader
 * (MISS<n>.DAT / TRN<n>.DAT -> SCRIPT.DAT byte copy and -> script.c loader callbacks),
 * shell_data_sys.md §3. */
#include "shell_data/sd_priv.h"

/* ---------------------------------------------------------------- 1efe memory stream */

/* PS 1efe:000e ms_open(p, len) */
void ms_open(u16 p, s16 len)
{
    DSW(PS_DS_ms_ptr) = p;
    DSW(PS_DS_ms_left) = (u16)len;
}

/* PS 1efe:001f ms_skip(n) — no bounds check */
s16 ms_skip(s16 n)
{
    DSW(PS_DS_ms_left) = (u16)(DSW(PS_DS_ms_left) - n);
    DSW(PS_DS_ms_ptr) = (u16)(DSW(PS_DS_ms_ptr) + n);
    return n;
}

/* PS 1efe:0033 ms_read(dst, size, n) -> items */
s16 ms_read(u16 dst, s16 size, s16 n)
{
    s16 k = 0;
    while (size <= (s16)DSW(PS_DS_ms_left) && n) {
        rtl_memcpy(dst, DSW(PS_DS_ms_ptr), (u16)size);
        DSW(PS_DS_ms_left) = (u16)(DSW(PS_DS_ms_left) - size);
        DSW(PS_DS_ms_ptr) = (u16)(DSW(PS_DS_ms_ptr) + size);
        dst = (u16)(dst + size);
        k++;
        n--;
    }
    return k;
}

/* PS 1efe:007e ms_tell */
u16 ms_tell(void) { return DSW(PS_DS_ms_ptr); }

/* ---------------------------------------------------------------- 1f06 snario.c */

/* PS 1f06:0008 mr_begin(p, len) */
static void mr_begin(u16 p, s16 len)
{
    ms_open(p, len);
    DSW(PS_DS_run_left) = 0;
}

/* PS 1f06:0020 mr_next — 1 while a record is available (a run of count 0 still yields one) */
static s16 mr_next(void)
{
    if (DSW(PS_DS_run_left) == 0) {
        if (ms_read(PS_DS_run_type, 2, 1) != 1) return 0;
        if (ms_read(PS_DS_run_left, 2, 1) != 1) sd_fatal(FS_SNARIO, 0x86);
    }
    DSW(PS_DS_run_left)--;
    return 1;
}

/* PS 1f06:0096 mr_skip — size table DS:1024 {90,22,16,6,10,58,0,2,2,1}; waypoints 2+12 / 2+4 */
static void mr_skip(void)
{
    s16 t = (s16)DSW(PS_DS_run_type);
    if (t == 7) {
        u16 m = stk_mark();
        u16 b = stk_alloc(2);
        ms_read(b, 2, 1);
        ms_skip(DSB(b) == 0 ? 12 : 4);
        stk_release(m);
        return;
    }
    if (t > 9 || t < 0) sd_fatal(FS_SNARIO, 0xA1);
    if (t == 6) sd_fatal(FS_SNARIO, 0xA2);
    ms_skip((s16)DSW((u16)(0x1024 + t * 2)));
}

/* PS 1f06:02c4 mission_prescan — counts for load_alloc */
static void mission_prescan(void)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x40);
    ds_far_wr(PS_DS_msg_ofs, far_null());
    ds_far_wr(PS_DS_msg_text, far_null());
    memzero(PS_DS_mf_scratch_hdr, 0x5A);
    if (DSW(DSW(PS_DS_mfile_buf)) != 4) sd_fatal(FS_SNARIO, 0x103);
    DSW(PS_DS_mf_n_type3) = DSW(PS_DS_mf_n_ships) = DSW(PS_DS_mf_n_aircraft) = 0;
    DSW(PS_DS_mf_n_groups) = DSW(PS_DS_mf_n_places) = 0;
    s16 ntypes = 2;
    mr_begin((u16)(DSW(PS_DS_mfile_buf) + 2), (s16)(DSW(PS_DS_mfile_len) - 2));
    while (mr_next()) {
        switch (DSW(PS_DS_run_type)) {
        case 0: ms_read(PS_DS_mf_scratch_hdr, 0x5A, 1); break;
        case 7:
            ms_read(buf, 2, 1);
            if (DSB(buf) == 0) {
                ms_skip(12);
                DSW(PS_DS_mf_wp_bytes) = (u16)(DSW(PS_DS_mf_wp_bytes) + 14);
            } else {
                if (DSC(buf) >= 6) sd_fatal(FS_SNARIO, 0x12E);
                ms_skip(4);
                DSW(PS_DS_mf_wp_bytes) = (u16)(DSW(PS_DS_mf_wp_bytes) + 4);
            }
            DSW(PS_DS_mf_n_waypoints)++;
            break;
        case 1: {
            ms_read(buf, 0x16, 1);
            u8 t = DSB(buf);
            if (t >= 0x9F) sd_fatal(FS_SNARIO, 0x13C);
            if (DSW(PS_DS_mf_ac_used + t * 2)++ == 0) ntypes++;
            DSW(PS_DS_mf_n_aircraft)++;
            break;
        }
        case 2: {
            ms_read(buf, 0x10, 1);
            u8 t = DSB(buf);
            if (t >= 0x73) sd_fatal(FS_SNARIO, 0x14D);
            if (DSW(PS_DS_mf_ship_used + t * 2)++ == 0) ntypes++;
            DSW(PS_DS_mf_n_ships)++;
            break;
        }
        case 3: DSW(PS_DS_mf_n_type3)++; break;           /* the record is not consumed (quirk) */
        case 4: DSW(PS_DS_mf_n_places)++; mr_skip(); break;
        case 5:
            ms_read(buf, 0x3A, 1);
            if (DSB(buf) == 1) {
                DSW(PS_DS_mf_n_ships)++;
                DSW(PS_DS_mf_ship_used) = 1;
            } else if (DSB(buf) != 0) sd_fatal(FS_SNARIO, 0x166);
            DSW(PS_DS_mf_n_groups)++;
            break;
        case 8: case 9: mr_skip(); break;
        default: sd_fatal(FS_SNARIO, 0x17C); break;
        }
    }
    DSW(PS_DS_mf_ntypes) = (u16)ntypes;
    stk_release(m);
}

/* PS 1f06:0553 mission_parse — records to the script.c loader callbacks */
static void mission_parse(void)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x3A), hdr = stk_alloc(2), pay = stk_alloc(12);
    ds_far_wr(PS_DS_msg_ofs, far_null());
    ds_far_wr(PS_DS_msg_text, far_null());
    memzero(PS_DS_mf_scratch_hdr, 0x5A);
    mr_begin((u16)(DSW(PS_DS_mfile_buf) + 2), (s16)(DSW(PS_DS_mfile_len) - 2));
    while (mr_next()) {
        switch (DSW(PS_DS_run_type)) {
        case 0: ms_read(PS_DS_mission_header, 0x5A, 1); break;
        case 7:
            ms_read(hdr, 2, 1);
            ms_read(pay, DSB(hdr) == 0 ? 12 : 4, 1);
            load_add_waypoint(hdr, pay);
            break;
        case 1: ms_read(buf, 0x16, 1); load_add_aircraft(buf); break;
        case 3: ms_read(buf, 6, 1); load_add3(buf); break;
        case 2: ms_read(buf, 0x10, 1); load_add_ship(buf); break;
        case 4: ms_read(buf, 0x0A, 1); load_add_place(buf); break;
        case 5: ms_read(buf, 0x3A, 1); load_add_group(buf); break;
        case 8:
            if (far_is_null(ds_far(PS_DS_msg_ofs))) {
                u16 n = (u16)((DSW(PS_DS_run_left) + 1) * 2);
                ds_far_wr(PS_DS_msg_ofs, xfarmalloc((u32)n));
                fmemcpy(ds_far(PS_DS_msg_ofs), ds_ptr(ms_tell()), n);
            }
            mr_skip();
            break;
        case 9:
            if (far_is_null(ds_far(PS_DS_msg_text))) {
                u16 n = (u16)(DSW(PS_DS_run_left) + 1);
                ds_far_wr(PS_DS_msg_text, xfarmalloc((u32)(s32)(s16)n));
                fmemcpy(ds_far(PS_DS_msg_text), ds_ptr(ms_tell()), n);
            }
            mr_skip();
            break;
        default: sd_fatal(FS_SNARIO, 0x1FF); break;
        }
    }
    stk_release(m);
}

/* PS 1f06:015c mission_load(name, parse) — "rb" DS:1038 */
void ps_mission_load(u16 name_ds, s8 parse)
{
    u16 f = my_fopen(name_ds, 0x1038);
    if (!f) sd_fatal_msg(FS_SNARIO, 0xC0, name_ds);
    memzero(PS_DS_mf_ntypes, 0x230);
    DSW(PS_DS_mfile_len) = (u16)my_filelength(f);
    DSW(PS_DS_mfile_buf) = xmalloc(DSW(PS_DS_mfile_len));
    if (!DSW(PS_DS_mfile_buf)) sd_fatal(FS_SNARIO, 0xC7);
    if (my_fread(DSW(PS_DS_mfile_buf), (s16)DSW(PS_DS_mfile_len), 1, f) == 0) sd_fatal(FS_SNARIO, 0xCA);
    my_fclose(f);
    if (parse) {
        mission_prescan();
        load_alloc(DSW(PS_DS_mf_n_aircraft), DSW(PS_DS_mf_n_ships), DSW(PS_DS_mf_n_places),
                   DSW(PS_DS_mf_n_waypoints), DSW(PS_DS_mf_n_groups), DSW(PS_DS_mf_n_type3));
        mission_parse();
        load_finish(name_ds);
    }
}

/* PS 1f06:0269 mission_write_script — SCRIPT.DAT = byte copy ("SCRIPT.DAT" DS:103B, "wb" DS:1046) */
void mission_write_script(void)
{
    u16 f = my_fopen(0x103B, 0x1046);
    if (!f) sd_fatal_msg(FS_SNARIO, 0xE6, 0x1049);
    my_fwrite(DSW(PS_DS_mfile_buf), (s16)DSW(PS_DS_mfile_len), 1, f);
    my_fclose(f);
}

/* PS 1f06:0782 mission_free */
void ps_mission_free_1f06(void) { xfree(DSW(PS_DS_mfile_buf)); }
