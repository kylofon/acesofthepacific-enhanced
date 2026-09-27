/* shell_data: shell\file.c (PS 1193) — SIM.IN / SIM.OUT, roster and records files, shell_data.md
 * §2-4. (1193:0115 make_name, 01b7 file_read, 01ff file_write, 027a/0306 control.prf and 0818..0856
 * realism .prf are platform's config.c.) */
#include "shell_data/sd_priv.h"

/* PS 1193:0006 enddate_get(campaign, force) — ENDDATE<n>.DAT cached in DS:0B6C */
s16 enddate_get(s8 a0, s8 a1)
{
    if (DSW(0x0B6C) == 0 || a1) {
        u16 f = my_fopen(make_name(0x0BDD, (s16)a0, 0x0BE5), 0x0BE9);
        if (!f) sd_fatal(FS_FILE, 0x73);
        my_fread(0x0B6C, 2, 1, f);
        my_fclose(f);
    }
    return (s16)DSW(0x0B6C);
}

/* PS 1193:0084 service_load — available services DS:49CE u16[11] from SERVICE<n>.DAT */
void service_load(void)
{
    rtl_memset(0x49CE, -1, 0x16);
    s16 t = table_load(0x0BEC, 0x0BF4, 2, 0, 0x11, 1, far_null(), far_null(), far_null());
    s16 n = table_count(t, 0);
    for (s16 i = 0; i < n; i++) DSW((u16)(0x49CE + i * 2)) = fw(table_rec(t, i), 0);
    table_free(t);
}

/* PS 1193:0176 file_read_n(buf, size, n, name) -> items (0 if no file) — "rb" DS:0BF8 */
s16 file_read_n(u16 p0_ds, s16 a1, s16 a2, u16 p3_ds)
{
    u16 f = my_fopen(p3_ds, 0x0BF8);
    if (!f) return 0;
    s16 k = my_fread(p0_ds, a1, a2, f);
    my_fclose(f);
    return k;
}

/* PS 1193:031e roster_read — "roster" DS:0C29 "dat" DS:0C30 "rb" DS:0C34 */
void roster_read(void)
{
    u16 f = my_fopen(make_name(0x0C29, -1, 0x0C30), 0x0C34);
    if (!f) { DSW(PS_DS_roster_count) = 0; return; }
    my_fread(PS_DS_roster_count, 2, 1, f);
    if (DSW(PS_DS_roster_count)) {
        ps_fread_far_1193(far_make(SEG(0x4b4c), 0x000E), 0x7A, DSW(PS_DS_roster_count), f);
        u16 m = stk_mark();
        u16 len = stk_alloc(2);
        my_fread(len, 2, 1, f);
        if (DSS(len) < 0 || DSS(len) > 0x200) sd_fatal(FS_FILE, 0x13E);
        if (DSW(len)) {
            if (my_fread(PS_DS_roster_text, 1, DSS(len), f) != DSS(len)) sd_fatal(FS_FILE, 0x143);
        }
        stk_release(m);
    }
    my_fclose(f);
}

/* PS 1193:0421 roster_write — "roster" DS:0C37 "dat" DS:0C3E "wb" DS:0C42 */
void roster_write(void)
{
    u16 f = my_fopen(make_name(0x0C37, -1, 0x0C3E), 0x0C42);
    if (!f) sd_fatal(FS_FILE, 0x15D);
    my_fwrite(PS_DS_roster_count, 2, 1, f);
    u16 m = stk_mark();
    u16 rec = stk_alloc(0x7A), sum = stk_alloc(2);
    DSW(sum) = 0;
    for (s16 i = 0; i < (s16)DSW(PS_DS_roster_count); i++) {
        for (u16 k = 0; k < 0x7A; k++) DSB(rec + k) = rd8(SEG(0x4b4c), (u16)(0x0E + i * 0x7A + k));
        DSW(sum) = (u16)(DSW(sum) + (u16)(s16)DSC(rec + 0x79));
        my_fwrite(rec, 0x7A, 1, f);
    }
    my_fwrite(sum, 2, 1, f);
    if (DSW(sum)) my_fwrite(PS_DS_roster_text, (s16)DSW(sum), 1, f);
    stk_release(m);
    my_fclose(f);
}

/* PS 1193:050d topace_read — "topace" DS:0C45 "dat" DS:0C4C "rb" DS:0C50; far 4a08:033b */
void topace_read(void)
{
    u16 f = my_fopen(make_name(0x0C45, -1, 0x0C4C), 0x0C50);
    if (!f) { DSW(PS_DS_topace_count) = 0; return; }
    my_fread(PS_DS_topace_count, 2, 1, f);
    if (DSW(PS_DS_topace_count)) {
        u16 m = stk_mark();
        u16 buf = stk_alloc(0x4C4);
        my_fread(buf, 0x7A, (s16)DSW(PS_DS_topace_count), f);
        for (s8 i = 0; (s16)i < (s16)DSW(PS_DS_topace_count); i++)
            for (u16 k = 0; k < 0x7A; k++)
                wr8(SEG(0x4a08), (u16)(0x33B + i * 0x7A + k), DSB(buf + i * 0x7A + k));
        stk_release(m);
    }
    my_fclose(f);
}

/* PS 1193:05b8 topmiss_read — "topmiss" DS:0C53 "dat" DS:0C5B "rb" DS:0C5F; far 4a08:0115 */
void topmiss_read(void)
{
    u16 f = my_fopen(make_name(0x0C53, -1, 0x0C5B), 0x0C5F);
    if (!f) { DSW(PS_DS_topmiss_count) = 0; return; }
    my_fread(PS_DS_topmiss_count, 2, 1, f);
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x226);
    my_fread(buf, 0x37, (s16)DSW(PS_DS_topmiss_count), f);
    for (s8 i = 0; (s16)i < (s16)DSW(PS_DS_topmiss_count); i++)
        for (u16 k = 0; k < 0x37; k++)
            wr8(SEG(0x4a08), (u16)(0x115 + i * 0x37 + k), DSB(buf + i * 0x37 + k));
    stk_release(m);
    my_fclose(f);
}

/* PS 1193:065c topace_write — "topace" DS:0C62 "dat" DS:0C69 "wb" DS:0C6D */
void topace_write(void)
{
    if (DSS(PS_DS_topace_count) < 0) DSW(PS_DS_topace_count) = 0;
    u16 f = my_fopen(make_name(0x0C62, -1, 0x0C69), 0x0C6D);
    if (!f) sd_fatal(FS_FILE, 0x1C5);
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x4C4);
    for (s8 i = 0; (s16)i < (s16)DSW(PS_DS_topace_count); i++)
        for (u16 k = 0; k < 0x7A; k++)
            DSB(buf + i * 0x7A + k) = rd8(SEG(0x4a08), (u16)(0x33B + i * 0x7A + k));
    my_fwrite(PS_DS_topace_count, 2, 1, f);
    my_fwrite(buf, 0x7A, (s16)DSW(PS_DS_topace_count), f);
    stk_release(m);
    my_fclose(f);
}

/* PS 1193:0727 topmiss_write — "topmiss" DS:0C70 "dat" DS:0C78 "wb" DS:0C7C; nothing if n == 0 */
s16 topmiss_write(void)
{
    if ((s16)DSW(PS_DS_topmiss_count) > 10) sd_fatal(FS_FILE, 0x1DA);
    if (DSW(PS_DS_topmiss_count) == 0) return 0;
    u16 f = my_fopen(make_name(0x0C70, -1, 0x0C78), 0x0C7C);
    if (!f) sd_fatal(FS_FILE, 0x1DF);
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x226);
    for (s8 i = 0; (s16)i < (s16)DSW(PS_DS_topmiss_count); i++)
        for (u16 k = 0; k < 0x37; k++)
            DSB(buf + i * 0x37 + k) = rd8(SEG(0x4a08), (u16)(0x115 + i * 0x37 + k));
    my_fwrite(PS_DS_topmiss_count, 2, 1, f);
    my_fwrite(buf, 0x37, (s16)DSW(PS_DS_topmiss_count), f);
    stk_release(m);
    my_fclose(f);
    return 0;
}

/* PS 1193:08b9 strings_load — "strings_" DS:0C93 "dat" DS:0C9C */
void ps_strings_load_1193(void)
{
    ps_strings_load_24e3(make_name(0x0C93, -1, 0x0C9C));
    strings_dup_far(0x5796, 3, 2);
    strings_dup_ranks();
    strings_free();
}

/* PS 1193:0881 shell_data_load */
void shell_data_load(void)
{
    roster_read();
    control_load();
    ps_strings_load_1193();
    tables_load();
    service_load();
    topace_read();
    topmiss_read();
}

/* PS 1193:08a4 shell_data_save (shell_exit) */
void shell_data_save(void)
{
    roster_write();
    control_save();
    topace_write();
    topmiss_write();
}

/* PS 1193:08f6 sim_in_read — "sim.in" DS:0CA0, 13 bytes at DS:4B86 */
s16 sim_in_read(void)
{
    s16 n = file_read(0x0CA0, PS_DS_sim_in, 13);
    if (n == 0) sd_fatal(FS_FILE, 0x28E);
    return n;
}

/* PS 1193:093e sim_in_build_write — shell_data.md §3.1 ("sim.in" DS:0CA7) */
void sim_in_build_write(void)
{
    u8 kind = DSB(MKIND);
    DSB(PS_DS_sim_in + 0) = kind == 1 ? DSB(0x4774) : 0xFF;
    if (kind == 2) DSB(PS_DS_sim_in + 1) = 0;
    else if (kind == 3) DSB(PS_DS_sim_in + 1) = 2;
    else if (kind == 4) DSB(PS_DS_sim_in + 1) = 3;
    else DSB(PS_DS_sim_in + 1) = kind;
    DSB(PS_DS_sim_in + 2) = DSB(MTYPE);
    DSW(PS_DS_sim_in + 4) = DSW(HDR + 8);
    DSB(PS_DS_sim_in + 6) = DSB(MAREA);
    DSB(PS_DS_sim_in + 8) = DSB(FLT(0, 0));
    DSW(PS_DS_sim_in + 10) = DSW(FLT(0, 0) + 0x18);
    DSB(PS_DS_sim_in + 9) = DSB(FLT(1, 0));
    if (kind == 1 || kind == 2) DSB(PS_DS_sim_in + 7) |= DSB((u16)(0x0B6E + (s16)(s8)module_active()));   /* bit[] = the 8-byte SCOPY of DS:0B6E */
    else DSB(PS_DS_sim_in + 7) = DSB(HDR + 6);
    file_write(0x0CA7, PS_DS_sim_in, 13);
}

/* PS 1193:09fe sim_out_read — "sim.out" DS:0CAE, 36 bytes at DS:4227 */
s16 sim_out_read(void)
{
    s16 n = file_read(0x0CAE, PS_DS_sim_out, 0x24);
    if (n != 0x24) sd_fatal(FS_FILE, 0x2CC);
    return n;
}

/* PS 1193:0a46 fread_far(far dst, size, n, fp) — pieces of 128/size items through DS:419A; the
 * whole piece is copied even after a short read (quirk kept) */
u16 ps_fread_far_1193(FarPtr a0, u16 a1, u16 a2, u16 p3_ds)
{
    u16 si = 0;
    u16 per = (u16)(0x80 / a1);
    while (si < a2) {
        u16 chunk = (u16)(a2 - si) > per ? per : (u16)(a2 - si);
        u16 k = (u16)my_fread(PS_DS_emd_bounce, (s16)a1, (s16)chunk, p3_ds);
        if (!k) break;
        u16 bytes = (u16)(chunk * a1);
        for (u16 i = 0; i < bytes; i++) { fwb(a0, 0, DSB(PS_DS_emd_bounce + i)); a0.off++; }
        si = (u16)(si + k);
    }
    return si;
}
