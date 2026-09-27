/* volume.c: the Dynamix volume file layer (platform_resources.md §6.1). INTRO seg 049D, PS/AP seg
 * 0000. Loose files first (data_prefix + name, then name), then the opened .DYN volumes by hashed
 * base name, then "<ext>\name". The handle of an open file is the DS offset of its VREC while the
 * volume layer is active, else a Borland FILE*. Everything goes through the ported RTL stdio, so the
 * FILE records, buffers and positions are those of the original.
 *
 * Program differences: INTRO/PS keep 10 open-file records, AP 3; AP skips volumes whose name contains
 * vol_exclude ("SH") and opens them sorted by vol_name_cmp; a bad signature is skipped silently by
 * INTRO/PS and asserts in AP; a volume that cannot be reopened is fatal_error in INTRO/PS and an
 * assert in AP. PORT: the debug log (vol_log, never enabled by the shipped callers) is fatal if a
 * log name is ever given. */
#include "platform/rtl_priv.h"

#include <string.h>

#include "codeptr.h"
#include "host.h"
#include "prog.h"
#include "symbols_ap.h"
#include "symbols_intro.h"
#include "symbols_ps.h"

/* VREC (12h bytes) */
#define VR_vol    0x00
#define VR_off    0x02
#define VR_size   0x06
#define VR_pos    0x0A
#define VR_inuse  0x0E
#define VR_fp     0x10
#define VR_SIZE   0x12
/* VOLUME (26h bytes, far array) */
#define VO_name   0x00
#define VO_index  0x0E
#define VO_fp     0x10
#define VO_filepos 0x12
#define VO_keys   0x18
#define VO_count  0x20
#define VO_dir    0x22
#define VO_SIZE   0x26

static int nrecs(void) { return prog_current_id() == PROG_AP ? 3 : 10; }

/* The per-program constants of the error paths (file-name far strings, line numbers). */
typedef struct { u16 file_seg, l_alloc, l_read, l_table, l_sig, l_select, fatal_fmt; } VolConsts;
static const VolConsts *vc(void)
{
    static const VolConsts c[4] = {
        [PROG_INTRO] = { 0x17D8, 0x2D1, 0x2D2, 0x305, 0, 0, 0x0832 },
        [PROG_PS]    = { 0x4CFD, 0x395, 0x396, 0x3C9, 0, 0, 0x2234 },
        [PROG_AP]    = { 0x3AEE, 0, 0, 0, 0x2C1, 0x3E1, 0 },
    };
    return &c[prog_current_id()];
}

static FarPtr vtab(void) { return ds_far(lds(G_vol_table)); }
static u16 vo_w(int v, u16 f) { FarPtr t = vtab(); return rd16(t.seg, (u16)(t.off + v * VO_SIZE + f)); }
static void vo_ww(int v, u16 f, u16 x) { FarPtr t = vtab(); wr16(t.seg, (u16)(t.off + v * VO_SIZE + f), x); }
static u32 vo_l(int v, u16 f) { FarPtr t = vtab(); return rd32(t.seg, (u16)(t.off + v * VO_SIZE + f)); }
static void vo_wl(int v, u16 f, u32 x) { FarPtr t = vtab(); wr32(t.seg, (u16)(t.off + v * VO_SIZE + f), x); }
static FarPtr vo_far(int v, u16 f) { FarPtr t = vtab(); return far_rd(t.seg, (u16)(t.off + v * VO_SIZE + f)); }

#define REC(i) ((u16)(lds(G_vol_files) + VR_SIZE * (i)))
#define RW(r, f)  DSW((u16)((r) + (f)))
#define RL_(r, f) DSL((u16)((r) + (f)))

/* A DS copy of a string constant on the emulated stack (the caller releases the stack mark). */
u16 ds_lit(const char *s)
{
    u16 n = (u16)strlen(s);
    u16 o = stk_alloc((u16)(n + 1));
    memcpy(DSP(o), s, n + 1);
    return o;
}

/* INTRO 049d:0008 / PS 0000:049e / AP 0000:0448 vol_ext_subdir_path: "<ext>\<name>" or NULL */
static u16 vol_ext_subdir_path(u16 name_ds)
{
    u16 dot = rtl_strchr(name_ds, '.');
    if (!dot) return 0;
    u16 buf = lds(G_vol_subdir_buf);
    u16 m = stk_mark();
    rtl_strcpy(buf, (u16)(dot + 1));
    rtl_strcat(buf, ds_lit("\\"));
    rtl_strcat(buf, name_ds);
    stk_release(m);
    return buf;
}

/* INTRO 049d:0059 / PS 0000:04ef / AP 0000:04e1 vol_set_log */
static void vol_set_log(u16 logname_ds)
{
    (void)logname_ds;
    host_fatal("vol_set_log: the volume debug log is not modelled");   /* PORT: never enabled */
}

/* INTRO 049d:008d / PS 0000:0523 / AP 0000:0517 vol_basename: after the last '\' */
static u16 vol_basename(u16 path_ds)
{
    u16 p = rtl_strrchr(path_ds, 0x5C);             /* '\\' */
    return p ? (u16)(p + 1) : path_ds;
}

/* INTRO 049d:1128 / PS 0000:15ab / AP 0000:15d9 vol_rec_of: handle -> in-use record (1-entry cache) */
static u16 vol_rec_of(u16 h)
{
    if (!h) {
        LW(G_vol_cache_handle) = 0;
        LW(G_vol_cache_rec) = 0;
        return 0;
    }
    if (!LW(G_vol_count)) return 0;
    if (h == LW(G_vol_cache_handle)) return LW(G_vol_cache_rec);
    LW(G_vol_cache_handle) = h;
    int n = nrecs(), i = 0;
    u16 si = REC(0);
    while (i < n && si != h) { si = (u16)(si + VR_SIZE); i++; }
    if (i == n || !RW(si, VR_inuse)) { si = 0; LW(G_vol_cache_handle) = 0; }
    LW(G_vol_cache_rec) = si;
    return si;
}

/* INTRO 049d:0fa3 / PS 0000:1426 / AP 0000:1457 vol_select: make volume v the open one (-1: close) */
static u16 vol_select(s16 v)
{
    s16 cur = LS(G_vol_cur);
    if (v != -1 && v == cur && LB(G_vol_open_files) != 0 && LB(G_vol_reopen_flag) == 0)
        return vo_w(cur, VO_fp);
    if (cur != -1 && vo_w(cur, VO_fp)) {
        rtl_fclose(vo_w(cur, VO_fp));
        vo_ww(cur, VO_fp, 0);
    }
    if (v != -1) {
        u16 m = stk_mark();
        u16 nm = stk_alloc(18);
        LB(G_vol_in_open) = 1;
        rtl__fstrncpy(ds_ptr(nm), far_add(vtab(), (u16)(v * VO_SIZE)), 14);
        u16 fp = rtl_fopen(nm, ds_lit("rb"));
        vo_ww(v, VO_fp, fp);
        if (!fp) {
            if (prog_current_id() == PROG_AP)
                assert_fail(far_make(SEG(vc()->file_seg), 0), (s16)vc()->l_select, nm);
            else
                fatal_error(vc()->fatal_fmt, nm);
        }
        LB(G_vol_in_open) = 0;
        vo_wl(v, VO_filepos, 0);
        stk_release(m);
    }
    LS(G_vol_cur) = v;
    vol_rec_of(0);
    LB(G_vol_reopen_flag) = 0;
    if (LS(G_vol_cur) == -1) return 0;
    return vo_w(LS(G_vol_cur), VO_fp);
}

/* INTRO 049d:10c1 / PS 0000:1544 / AP 0000:1586 vol_seek */
static void vol_seek(u32 pos)
{
    s16 cur = LS(G_vol_cur);
    if (cur == -1 || vo_l(cur, VO_filepos) == pos) return;
    rtl_fseek(vo_w(cur, VO_fp), (s32)pos, 0);
    vo_wl(cur, VO_filepos, pos);
}

/* INTRO 049d:0e32 / PS 0000:12b9 / AP 0000:12ea vol_find_entry: hash lookup in one volume */
static s16 vol_find_entry(u16 name_ds, u16 rec, s16 v)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x16);
    u16 bn = vol_basename(name_ds);
    u16 len = (u16)(ds_strlen(bn) + 1);              /* strncpy(buf, basename, 13) */
    for (u16 i = 0; i < 13; i++) DSB((u16)(buf + i)) = i < len ? DSB((u16)(bn + i)) : 0;
    rtl_strupr(buf);
    u16 l = ds_strlen(buf);
    if (l == 0) host_fatal("vol_find_entry: empty name (division by zero in the original)");
    u32 h = 0;
    for (int i = 0; i < 13; i++)
        h = rtl__lrotl(h, (s16)vo_w(v, (u16)(VO_keys + 2 * (i % 4)))) ^ (u32)(s32)DSC((u16)(buf + i % l));
    u16 key = (u16)(h % 0x1003Fu);
    FarPtr d = vo_far(v, VO_dir);
    u16 count = vo_w(v, VO_count);
    u32 acc = 0;
    s16 i = 0;
    while ((s16)count > i && rd16(d.seg, d.off) != key) {
        i++;
        acc += rd16(d.seg, (u16)(d.off + 2));
        d.off = (u16)(d.off + 4);
    }
    acc += rd16(d.seg, (u16)(d.off + 2));            /* d[count] read out of bounds when not found */
    acc *= 4;
    stk_release(m);
    if (rd16(d.seg, d.off) != key) return 0;
    RW(rec, VR_vol) = (u16)v;
    RL_(rec, VR_off) = acc;
    RL_(rec, VR_size) = 0;
    RL_(rec, VR_pos) = 0;
    return 1;
}

/* INTRO 049d:00bb / PS 0000:0551 / AP 0000:0555 my_fopen */
u16 my_fopen(u16 name_ds, u16 mode_ds)
{
    if (LW(G_vol_log)) host_fatal("my_fopen: volume log not modelled");
    u16 m = stk_mark();
    u16 tmp = stk_alloc(0x50);                       /* [bp-66h] */
    u16 result = 0;
    if (LW(G_vol_count) == 0) {
        rtl_strcpy(tmp, lds(G_data_prefix));
        rtl_strcat(tmp, name_ds);
        u16 f = rtl_fopen(tmp, mode_ds);
        if (!f) f = rtl_fopen(name_ds, mode_ds);
        stk_release(m);
        return f;
    }
    if (LB(G_vol_reopen_flag)) vol_select(-1);
    int n = nrecs(), k = n;
    u16 rec = REC(0);
    while (k && RW(rec, VR_inuse)) { k--; rec = (u16)(rec + VR_SIZE); }
    if (!k) goto out;
    u16 fp;
    do {                                             /* 1. loose file (data_prefix + name, name) */
        LB(G_vol_in_open) = 1;
        rtl_strcpy(tmp, lds(G_data_prefix));
        rtl_strcat(tmp, name_ds);
        fp = rtl_fopen(tmp, mode_ds);
        if (!fp) fp = rtl_fopen(name_ds, mode_ds);
        LB(G_vol_crit_error) = 0;
    } while (LB(G_vol_crit_error));
    LB(G_vol_in_open) = 0;
    if (fp) {
        RW(rec, VR_inuse) = 1;
        RW(rec, VR_fp) = fp;
        LB(G_vol_open_files)++;
        result = rec;
        goto out;
    }
    /* 2. the volumes: vol_cur, upwards, then downwards */
    s16 cur = LS(G_vol_cur), up = cur, down = cur;
    s16 found;
    do {
        found = vol_find_entry(name_ds, rec, cur);
        if (found) {
            vol_select((s16)RW(rec, VR_vol));
            vol_seek(RL_(rec, VR_off) + RL_(rec, VR_pos));
            u16 v = vo_w(LS(G_vol_cur), VO_fp);
            u16 nm = stk_alloc(0x16);                /* [bp-16h] */
            rtl_fread(nm, 13, 1, v);
            rtl_fread((u16)(rec + VR_size), 4, 1, v);
            u32 pos = (u32)rtl_ftell(v);
            RL_(rec, VR_off) = pos;
            vo_wl(LS(G_vol_cur), VO_filepos, pos);
            if (rtl_stricmp(nm, vol_basename(name_ds)) != 0) found = 0;
        }
        if (!found) {
            if ((s16)(LW(G_vol_count) - 1) > up) cur = ++up;
            else cur = --down;
        }
    } while (!found && down >= 0);
    if (found) {
        RL_(rec, VR_pos) = 0;
        RW(rec, VR_fp) = 0;
        RW(rec, VR_inuse) = 1;
        LB(G_vol_open_files)++;
        result = rec;
        goto out;
    }
    /* 3. "<ext>\<name>" */
    LW(G_vol_last_subdir) = vol_ext_subdir_path(name_ds);
    if (!LW(G_vol_last_subdir)) goto out;
    LB(G_vol_in_open) = 1;
    do {
        LB(G_vol_crit_error) = 0;
        fp = rtl_fopen(LW(G_vol_last_subdir), mode_ds);
    } while (LB(G_vol_crit_error));
    LB(G_vol_in_open) = 0;
    if (fp) {
        RW(rec, VR_inuse) = 1;
        RW(rec, VR_fp) = fp;
        LB(G_vol_open_files)++;
        result = rec;
    }
out:
    stk_release(m);
    return result;
}

/* INTRO 049d:03bc / PS 0000:084b / AP 0000:08cc my_fclose */
s16 my_fclose(u16 f)
{
    s16 r = 0;
    u16 rec;
    if (!LW(G_vol_count) || !(rec = vol_rec_of(f))) return rtl_fclose(f);
    vol_rec_of(0);
    if (RW(rec, VR_fp)) r = rtl_fclose(RW(rec, VR_fp));
    RW(rec, VR_inuse) = 0;
    LB(G_vol_open_files)--;
    return r;
}

/* Returns the volume record of an entry handle (and 0 with *fp set for a loose/real FILE). */
static u16 entry_of(u16 f, u16 *fp)
{
    *fp = f;
    if (!LW(G_vol_count)) return 0;
    u16 rec = vol_rec_of(f);
    if (!rec) return 0;
    *fp = RW(rec, VR_fp);
    return *fp ? 0 : rec;
}

/* INTRO 049d:0411 / PS 0000:08a0 / AP 0000:0945 my_fread */
s16 my_fread(u16 buf_ds, s16 size, s16 n, u16 f)
{
    u16 fp;
    u16 rec = entry_of(f, &fp);
    if (!rec) return (s16)rtl_fread(buf_ds, (u16)size, (u16)n, fp);
    u16 vfp = vol_select((s16)RW(rec, VR_vol));
    vol_seek(RL_(rec, VR_off) + RL_(rec, VR_pos));
    u16 dx = (u16)(size * n);
    u32 left = RL_(rec, VR_size) - RL_(rec, VR_pos);
    while (dx != 0 && (u32)dx > left) { n--; dx = (u16)(dx - size); }
    u16 got = rtl_fread(buf_ds, (u16)size, (u16)n, vfp);
    u32 add = (u16)(got * size);
    RL_(rec, VR_pos) += add;
    vo_wl(RW(rec, VR_vol), VO_filepos, vo_l(RW(rec, VR_vol), VO_filepos) + add);
    return (s16)got;
}

/* INTRO 049d:0616 / PS 0000:0aa1 / AP 0000:0a0d file_hread: huge read on a real FILE via its handle,
 * all or nothing */
static s32 file_hread(FarPtr buf, s32 size, s32 n, u16 fp)
{
    s16 fd = DSC((u16)(fp + FILE_fd));
    s32 p = rtl_ftell(fp);
    if (p == -1) return 0;
    if (rtl_lseek(fd, p, 0) == -1) { rtl_fseek(fp, p, 0); return 0; }
    u32 total = (u32)(size * n);
    if (lib_hread(buf, total, fd) != total) { rtl_fseek(fp, p, 0); return 0; }
    rtl_fseek(fp, rtl_tell(fd), 0);
    return n;
}

/* INTRO 049d:04f4 / PS 0000:0983 my_hread (huge read clamped to the entry) */
s32 my_hread(FarPtr buf, s32 size, s32 n, u16 f)
{
    u16 fp;
    u16 rec = entry_of(f, &fp);
    if (!rec) return file_hread(buf, size, n, fp);
    u16 vfp = vol_select((s16)RW(rec, VR_vol));
    vol_seek(RL_(rec, VR_off) + RL_(rec, VR_pos));
    u32 bytes = (u32)(size * n);
    u32 left = RL_(rec, VR_size) - RL_(rec, VR_pos);
    while (bytes != 0 && bytes > left) { n--; bytes = (u32)(size * n); }
    s32 got = file_hread(buf, size, n, vfp);
    (void)got;
    u32 add = (u32)(size * n);                       /* the clamped n, not got (original) */
    RL_(rec, VR_pos) += add;
    vo_wl(RW(rec, VR_vol), VO_filepos, vo_l(RW(rec, VR_vol), VO_filepos) + add);
    return got;
}

/* INTRO 049d:06d1 / PS 0000:0b5a / AP 0000:0ae8 my_fseek (entry: SEEK_END = size - off, clamped) */
s16 my_fseek(u16 f, s32 off, s16 whence)
{
    u16 fp;
    u16 rec = entry_of(f, &fp);
    if (!rec) return rtl_fseek(fp, off, whence);
    u32 o = (u32)off;
    u32 size = RL_(rec, VR_size);
    if (whence == 1) o += RL_(rec, VR_pos);
    else if (whence == 2) o = size > o ? size - o : 0;
    if (size < o) o = size;
    RL_(rec, VR_pos) = o;
    return 0;
}

/* INTRO 049d:078e / PS 0000:0c17 / AP 0000:0b79 my_ftell */
s32 my_ftell(u16 f)
{
    u16 fp;
    u16 rec = entry_of(f, &fp);
    if (!rec) return rtl_ftell(fp);
    return (s32)RL_(rec, VR_pos);
}

/* INTRO 049d:07ca / PS 0000:0c53 / AP 0000:0bba my_filelength */
s32 my_filelength(u16 f)
{
    u16 fp;
    u16 rec = entry_of(f, &fp);
    if (rec) return (s32)RL_(rec, VR_size);
    s32 here = rtl_ftell(fp);
    rtl_fseek(fp, 0, 2);
    s32 end = rtl_ftell(fp);
    rtl_fseek(fp, here, 0);
    return end;
}

/* INTRO 049d:084d / PS 0000:0cd6 / AP 0000:0c44 my_rewind */
void my_rewind(u16 f) { my_fseek(f, 0, 0); }

/* INTRO 049d:0865 / PS 0000:0cee / AP 0000:0c58 my_fgetc */
s16 my_fgetc(u16 f)
{
    u16 fp;
    u16 rec = entry_of(f, &fp);
    if (!rec) return rtl_fgetc(fp);
    if (RL_(rec, VR_pos) >= RL_(rec, VR_size)) return -1;
    vol_select((s16)RW(rec, VR_vol));
    vol_seek(RL_(rec, VR_off) + RL_(rec, VR_pos));
    s16 c = rtl_fgetc(vo_w(RW(rec, VR_vol), VO_fp));
    RL_(rec, VR_pos) += 1;
    vo_wl(RW(rec, VR_vol), VO_filepos, vo_l(RW(rec, VR_vol), VO_filepos) + 1);
    return c;
}

/* INTRO 049d:0913 / PS 0000:0d9c / AP 0000:0d02 my_feof */
s16 my_feof(u16 f)
{
    u16 fp;
    u16 rec = entry_of(f, &fp);
    if (!rec) return (s16)(DSW((u16)(fp + FILE_flags)) & F_EOF);
    return RL_(rec, VR_pos) >= RL_(rec, VR_size) ? 1 : 0;
}

/* INTRO 049d:0961 / PS 0000:0dea / AP 0000:0d48 my_fwrite (0 for a volume entry) */
s16 my_fwrite(u16 buf_ds, s16 size, s16 n, u16 f)
{
    u16 fp;
    u16 rec = entry_of(f, &fp);
    if (rec) return 0;
    return (s16)rtl_fwrite(buf_ds, (u16)size, (u16)n, fp);
}

/* INTRO 049d:09b0 / PS 0000:0e39 / AP 0000:0d8d my_fputc (-1 for a volume entry) */
s16 my_fputc(s16 c, u16 f)
{
    u16 fp;
    u16 rec = entry_of(f, &fp);
    if (rec) return -1;
    return rtl_fputc(c, fp);
}

/* INTRO 049d:09f8 / PS 0000:0e81 / AP 0000:0dcd my_setbuf (nothing for a volume entry) */
static void my_setbuf(u16 f, u16 buf_ds)
{
    u16 fp;
    u16 rec = entry_of(f, &fp);
    if (!rec) rtl_setbuf(fp, buf_ds);
}

/* INTRO 049d:118b / PS 0000:160e / AP 0000:163f vol_real_file (no callers) */
static u16 vol_real_file(u16 f)
{
    u16 rec = vol_rec_of(f);
    return rec ? RW(rec, VR_fp) : 0;
}

/* INTRO 049d:0e28 / PS 0000:12af / AP 0000:12e0 vol_force_reopen (no callers) */
static void vol_force_reopen(void) { LB(G_vol_reopen_flag) = 1; }

/* INTRO 049d:11ab / PS 0000:162e / AP 0000:165e vol_int24: DOS critical-error handler. PORT: the
 * host never raises critical errors; kept for the vector and as documentation. Returns AL. */
static u8 vol_int24(void)
{
    u8 al = LB(G_vol_in_open) ? 3 : 1;               /* fail inside a layer fopen, else retry */
    LB(G_vol_crit_error) = 1;
    LB(G_vol_reopen_flag) = 1;
    return al;
}

/* INTRO 049d:0a2c / PS 0000:0eb5 / AP 0000:0e04 vol_open_index: read a volume header + directory */
static void vol_open_index(u16 volname_ds, u16 mask)
{
    u16 m = stk_mark();
    u16 fp = rtl_fopen(volname_ds, ds_lit("rb"));
    if (!fp) { stk_release(m); return; }
    bool ap = prog_current_id() == PROG_AP;
    u16 hdr = ap ? stk_alloc(0x16) : lds(G_vol_hdr_buf);
    u16 flags = stk_alloc(2), keys = stk_alloc(8), cnt = stk_alloc(2);
    rtl_fread(hdr, 0x16, 1, fp);
    if (strcmp((const char *)DSP(hdr), "Dynamix Volume File") != 0) {
        if (!ap) goto close;
        assert_fail(far_make(SEG(vc()->file_seg), 0), (s16)vc()->l_sig, volname_ds);
    }
    rtl_fread(flags, 2, 1, fp);
    u16 fm = DSW(flags) & mask;
    if (!(fm & 0xF000) || !(fm & 0x0FFF)) goto close;
    rtl_fread(keys, 2, 4, fp);
    rtl_fread(cnt, 2, 1, fp);
    u16 n = DSW(cnt);
    if (n == 0) goto close;
    while (rtl_ftell(fp) % 4) rtl_fseek(fp, 1, 1);
    FarPtr dir = dos_alloc((u32)(u16)(n << 2), 0, 1);
    if (!ap && far_is_null(dir))
        assert_fail(far_make(SEG(vc()->file_seg), 0), (s16)vc()->l_alloc, 0);
    s32 r = file_hread(dir, (s32)(u16)(n << 2), 1, fp);
    if (!ap && r != 1)
        assert_fail(far_make(SEG(vc()->file_seg), 0), (s16)vc()->l_read, 0);
    int v = (s16)LW(G_vol_count);
    rtl__fstrncpy(far_add(vtab(), (u16)(v * VO_SIZE)), ds_ptr(volname_ds), 14);
    far_wr(vtab().seg, (u16)(vtab().off + v * VO_SIZE + VO_dir), dir);
    vo_ww(v, VO_index, (u16)v);
    vo_ww(v, VO_count, n);
    for (int i = 0; i < 4; i++) vo_ww(v, (u16)(VO_keys + 2 * i), DSW((u16)(keys + 2 * i)));
    LW(G_vol_count)++;
close:
    rtl_fclose(fp);
    stk_release(m);
}

/* REPE CMPSB over strlen(b)+1 bytes, then SBB AX,AX / SBB AX,-1: -1 / 0 / 1 */
static s16 strcmp_sign(u16 a, u16 b)
{
    u16 n = (u16)(ds_strlen(b) + 1), i = 0;
    u8 x, y;
    do { x = DSB((u16)(a + i)); y = DSB((u16)(b + i)); i++; } while (--n && x == y);
    return x == y ? 0 : x < y ? -1 : 1;
}

/* AP 0000:0fde vol_name_cmp: names starting with 'M' first, in reverse order (qsort comparator) */
static s16 vol_name_cmp(u16 a_ds, u16 b_ds)
{
    bool am = DSB(a_ds) == 'M', bm = DSB(b_ds) == 'M';
    if (am && bm) { s16 c = strcmp_sign(a_ds, b_ds); return (s16)-c; }
    if (am) return -1;
    if (bm) return 1;
    return strcmp_sign(a_ds, b_ds);
}

/* INTRO 049d:0c4a / PS 0000:10d1 / AP 0000:1057 vol_init: scan *.DYN, open the matching volumes */
void vol_init(u16 log_ds, u16 a, u16 b)
{
    int id = prog_current_id();
    u16 mask = id == PROG_INTRO ? 0x2001 : 0x8001;
    if (LB(G_vol_inited)) return;
    if (a || b) mask = a | b;
    if (log_ds) vol_set_log(log_ds);
    ds_far_wr(lds(G_vol_old_int24), ivt_get(0x24));
    ivt_set(0x24, codeptr_far_shared(F_vol_int24));  /* PORT: never raised (host files) */
    u16 m = stk_mark();
    u16 ff = stk_alloc(0x2C);                        /* struct ffblk */
    s16 r = rtl_findfirst(ds_lit("*.DYN"), ff, 0);
    if (r == 0 && far_is_null(vtab())) {
        FarPtr t = dos_alloc(0x17C, 0, 1);
        ds_far_wr(lds(G_vol_table), t);
        if (id != PROG_AP && far_is_null(t))
            assert_fail(far_make(SEG(vc()->file_seg), 0), (s16)vc()->l_table, 0);
    }
    if (id != PROG_AP) {
        while (r == 0 && (s16)LW(G_vol_count) < 10) {
            vol_open_index((u16)(ff + 0x1E), mask);
            r = rtl_findnext(ff);
        }
    } else if (r == 0) {
        u16 names = stk_alloc(13 * 10), up = stk_alloc(0x14), ex = stk_alloc(0x14);
        s16 n = 0;
        if (DSW(AP_DS_vol_exclude)) { rtl_strcpy(ex, DSW(AP_DS_vol_exclude)); rtl_strupr(ex); }
        while (r == 0 && n < 10) {
            rtl_strcpy(up, (u16)(ff + 0x1E));
            rtl_strupr(up);
            if (!DSW(AP_DS_vol_exclude) || !rtl_strstr(up, ex)) {
                rtl_strcpy((u16)(names + 13 * n), up);
                n++;
            }
            r = rtl_findnext(ff);
        }
        rtl_qsort(names, (u16)n, 13, codeptr_far(AP_FN_vol_name_cmp));
        for (s16 i = 0; i < n; i++) vol_open_index((u16)(names + 13 * i), mask);
    }
    stk_release(m);
    if (LW(G_vol_count)) LW(G_vol_active) = 1;
    LB(G_vol_inited) = 1;
}

/* INTRO 049d:0d50 / PS 0000:11d7 / AP 0000:122b vol_shutdown */
void vol_shutdown(void)
{
    if (LW(G_vol_log)) {
        rtl_fclose(LW(G_vol_log));
        if (prog_current_id() != PROG_AP) LW(G_vol_log) = 0;
    }
    if (!LB(G_vol_inited)) return;
    if (!far_is_null(vtab())) {
        for (int i = 0; i < (s16)LW(G_vol_count); i++) {
            FarPtr d = vo_far(i, VO_dir);
            if (!far_is_null(d)) {
                dos_free(d);
                far_wr(vtab().seg, (u16)(vtab().off + i * VO_SIZE + VO_dir), far_make(0, 0));
            }
        }
        dos_free(vtab());
    }
    ds_far_wr(lds(G_vol_table), far_make(0, 0));
    LW(G_vol_count) = 0;
    FarPtr old = ds_far(lds(G_vol_old_int24));
    if (!far_is_null(old)) {
        ivt_set(0x24, old);
        ds_far_wr(lds(G_vol_old_int24), far_make(0, 0));
    }
    LB(G_vol_inited) = 0;
}

/* AP 0000:1a70 int24_install / 0000:1ab0 int24_restore: "retry" handler for disk errors.
 * PORT: nothing to do (host files never raise critical errors); the flag and vector are kept. */
void int24_install(void) { }
void int24_restore(void) { }

/* Code pointers into volume.c stored in data: AP's qsort comparator, the INT 24h vector. */
void volume_register(void)
{
    codeptr_register(PROG_AP, AP_FN_vol_name_cmp, (CodeFn)vol_name_cmp);
    codeptr_register_shared(F_vol_int24, (CodeFn)vol_int24);
    (void)vol_real_file;
    (void)vol_force_reopen;
    (void)my_setbuf;
}
