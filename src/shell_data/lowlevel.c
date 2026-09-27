/* shell_data: low-level helpers of PS emd\* (shell_data_emd1.md §1, shell_data.md §1):
 * wdutil.c far lists (242f), binary stream I/O (249b), the RNG (24b7), string pool (24d5),
 * merged text tables text.c (26c4), the emd sequential reader emd.c (26fc), counted lists list.c (273b). */
#include "shell_data/sd_priv.h"

/* ================================================================================================
 * 242f wdutil.c — singly linked far lists (+00 next, key u16 at +04)
 * ================================================================================================ */

/* PS 242f:0004 flist_insert_sorted — shell_data_emd1.md §1.1 */
FarPtr flist_insert_sorted(FarPtr head, FarPtr n)
{
    if (far_is_null(head) || fw(n, 4) < fw(head, 4)) {
        fwp(n, 0, head);
        return n;
    }
    u16 key = fw(n, 4);
    FarPtr p = head;
    for (;;) {
        FarPtr nx = fp(p, 0);
        if (far_is_null(nx) || key < fw(nx, 4)) break;
        p = nx;
    }
    fwp(n, 0, fp(p, 0));
    fwp(p, 0, n);
    return head;
}

/* PS 242f:00ad flist_append — shell_data_emd1.md §1.1 */
FarPtr flist_append(FarPtr head, FarPtr n)
{
    if (far_is_null(head)) {
        fwp(n, 0, far_null());
        return n;
    }
    FarPtr p = head;
    while (!far_is_null(fp(p, 0))) p = fp(p, 0);
    fwp(p, 0, n);
    return head;
}

/* PS 242f:0122 flist_unlink — shell_data_emd1.md §1.1 */
FarPtr flist_unlink(FarPtr head, FarPtr n)
{
    if (far_is_null(head)) sd_fatal(FS_WDUTIL, 0x3B);
    if (far_eq(n, head)) {
        FarPtr nx = fp(n, 0);
        fwp(n, 0, far_null());
        return nx;
    }
    FarPtr p = head;
    for (;;) {
        FarPtr nx = fp(p, 0);
        if (far_is_null(nx)) { sd_fatal(FS_WDUTIL, 0x47); return head; }
        if (far_eq(nx, n)) break;
        p = nx;
    }
    fwp(p, 0, fp(n, 0));
    fwp(n, 0, far_null());
    return head;
}

/* PS 242f:0224 flist_count (no callers) */
s16 flist_count(FarPtr head)
{
    s16 n = 0;
    for (FarPtr p = head; !far_is_null(p); p = fp(p, 0)) n++;
    return n;
}

/* PS 242f:025b wdutil_fragment: 3 bytes (push bp; mov bp,sp) at the end of the segment, not a
 * function — nothing to port. */

/* ================================================================================================
 * 249b binary stream I/O (getc = 0000:0cee my_fgetc, putc = 0000:0e39 my_fputc)
 * ================================================================================================ */

/* PS 249b:0007 rd_u16 — no EOF check: a + b*256 in 16 bits */
s16 rd_u16(u16 f)
{
    u16 a = (u16)my_fgetc(f);
    u16 b = (u16)my_fgetc(f);
    return (s16)(u16)(a + (u16)(b << 8));
}

/* PS 249b:002d rd_s32 — (s32)b0 + ((s32)b1 << 8) + (b2 into the high word) + ((s32)b3 << 24) */
s32 rd_s32(u16 f)
{
    u32 v = (u32)(s32)my_fgetc(f);
    v += (u32)((s32)my_fgetc(f) << 8);
    v += (u32)(u16)my_fgetc(f) << 16;
    v += (u32)((s32)my_fgetc(f) << 24);
    return (s32)v;
}

/* PS 249b:008a rd_cstring — into DS:541C (no bound), NULL at EOF or for an empty string */
FarPtr rd_cstring(u16 f)
{
    DSW(PS_DS_rd_strbuf_ptr) = PS_DS_rd_strbuf;
    s16 c;
    do {
        c = my_fgetc(f);
        if (c == -1) break;
        u16 p = DSW(PS_DS_rd_strbuf_ptr);
        DSW(PS_DS_rd_strbuf_ptr) = (u16)(p + 1);
        DSB(p) = (u8)c;
    } while ((u8)c != 0);
    if (c == -1 || DSC(PS_DS_rd_strbuf) == 0) return far_null();
    return ds_ptr(PS_DS_rd_strbuf);
}

/* PS 249b:00cf wr_u16 */
s16 wr_u16(u16 v, u16 f)
{
    if (my_fputc((s16)(v & 0xFF), f) == -1) return 0;
    if (my_fputc((s16)(v >> 8), f) == -1) return 0;
    return 1;
}

/* PS 249b:010e wr_u32 (no callers) */
s16 wr_u32(u32 v, u16 f)
{
    for (int i = 0; i < 4; i++)
        if (my_fputc((s16)((v >> (8 * i)) & 0xFF), f) == -1) return 0;
    return 1;
}

/* PS 249b:0185 wr_cstring — chars of s (if any) then a NUL; returns putc != EOF */
s16 wr_cstring(u16 s_ds, u16 f)
{
    if (s_ds)
        for (u16 p = s_ds; DSB(p); p++)
            if (my_fputc(DSB(p), f) == -1) return 0;
    return my_fputc(0, f) != -1;
}

/* ================================================================================================
 * 24b7 the RNG — additive lagged Fibonacci (shell_data.md §1)
 * ================================================================================================ */

/* PS 24b7:000c rng_next */
u16 ps_rng_next(u16 st)
{
    (void)st;
    s16 k = (s16)DSC(PS_DS_rng_k), j = (s16)DSC(PS_DS_rng_j);
    u16 a = DSW(PS_DS_rng_tab + k * 2);
    u16 r = (u16)(DSW(PS_DS_rng_tab + j * 2) + a);
    DSW(PS_DS_rng_tab + j * 2) = r;
    if (++DSB(PS_DS_rng_j) == 0x38) DSB(PS_DS_rng_j) = 0;
    if (++DSB(PS_DS_rng_k) == 0x38) DSB(PS_DS_rng_k) = 0;
    if (++DSW(PS_DS_rng_calls) == 0x400) DSW(PS_DS_rng_calls) = 0;
    return r;
}

/* PS 24b7:0070 rng_seed — table from far 4c91:0000 (0x70 bytes, via 16e3:49c1) */
void ps_rng_seed(u16 s)
{
    for (u16 i = 0; i < 0x70; i++) DSB(PS_DS_rng_tab + i) = rd8(SEG(0x4c91), i);
    DSB(PS_DS_rng_k) = 0x18;
    DSB(PS_DS_rng_j) = 0x37;
    s = (u16)(s % 0x400);
    DSW(PS_DS_rng_calls) = 0;
    while (s--) ps_rng_next(0);
}

/* PS 24b7:00be rng_count */
u16 rng_count(void) { return DSW(PS_DS_rng_calls); }

/* PS 24b7:00c8 rng_below — n < 2: 0 without a draw */
s16 rng_below(s16 n)
{
    if (n <= 1) return 0;
    s16 v = (s16)(ps_rng_next(0) & 0xFFF);
    if (sd_rng_trace) sd_rng_trace(n, v % n);
    return (s16)(v % n);
}

/* PS 24b7:00eb rng_below_long (only called by the uncalled 0151): two draws, LMOD */
s32 rng_below_long(s32 n)
{
    if (n == 0) return 0;
    u16 hi = (u16)(ps_rng_next(0) & 0xFFF);
    u16 lo = ps_rng_next(0);
    s32 v = (s32)((u32)hi << 16 | lo);
    if (sd_rng_trace) sd_rng_trace(-1, v % n);
    return v % n;
}

/* PS 24b7:0132 rng_range (uncalled) */
s32 rng_range(s16 lo, s16 hi) { return (s32)(s16)(lo + rng_below((s16)(hi - lo + 1))); }

/* PS 24b7:0151 rng_range_long (uncalled) */
s32 rng_range_long(s32 lo, s32 hi) { return (s32)((u32)lo + (u32)rng_below_long((s32)((u32)hi - (u32)lo + 1))); }

/* PS 24b7:0180 rng_pick_weighted(int *pct, int n) — inclusive bounds, n-1 fall-through */
s8 rng_pick_weighted(u16 p0_ds, s16 n)
{
    s16 lo = 0, hi = DSS(p0_ds);
    s16 r = rng_below(100);
    if (r == 0) r = 1;
    s16 i = 1;
    for (; i < n; i++) {
        if (r >= lo && r <= hi) break;
        lo = hi;
        hi = (s16)(hi + DSS(p0_ds + i * 2));
    }
    return (s8)(i - 1);
}

/* PS 24b7:01d1 rng_seed_time */
void rng_seed_time(void) { ps_rng_seed((u16)rtl_time(0)); }

/* ================================================================================================
 * 24d5 string pool / far-near strcpy
 * ================================================================================================ */

/* PS 24d5:0005 strpool_copy — ring pool DS:5592..5785, cursor DS:1DB2; returns a near pointer
 * (FarPtr in the header: DX is DS) */
FarPtr strpool_copy(FarPtr s)
{
    u16 len = rtl__fstrlen(s);
    u16 cur = DSW(0x1DB2);
    if ((u16)(cur + len + 1) > 0x5785) cur = PS_DS_strpool;
    u16 r = cur;
    u16 i = 0;
    for (;;) {
        u8 c = fb(s, i);
        DSB(cur + i) = c;
        i++;
        if (!c) break;
    }
    DSW(0x1DB2) = (u16)(cur + len + 1);
    return ds_ptr(r);
}

/* PS 24d5:0053 fstrcpy_near_src(char far *dst, char *src) */
FarPtr fstrcpy_near_src(FarPtr dst, u16 src_ds)
{
    u16 i = 0;
    for (;;) {
        u8 c = DSB(src_ds + i);
        fwb(dst, i, c);
        i++;
        if (!c) break;
    }
    return dst;
}

/* PS 24d5:0091 strcpy_far_src(char *dst, char far *src) */
FarPtr strcpy_far_src(u16 dst_ds, FarPtr src)
{
    u16 i = 0;
    for (;;) {
        u8 c = fb(src, i);
        DSB(dst_ds + i) = c;
        i++;
        if (!c) break;
    }
    return ds_ptr(dst_ds);
}

/* PS 24d5:00bd strcat_far_src (no callers) */
FarPtr strcat_far_src(u16 dst_ds, FarPtr src)
{
    return strcpy_far_src((u16)(dst_ds + rtl_strlen(dst_ds)), src);
}

/* ================================================================================================
 * 26fc emd.c — one global sequential stream: DS:5AFA FILE*, DS:5AF8 u16 bytes remaining
 * ================================================================================================ */

/* PS 26fc:000c emd_set_stream */
void emd_set_stream(u16 f)
{
    DSW(PS_DS_emd_file) = f;
    if (!f) sd_fatal(FS_EMD, 0x10);
    DSW(PS_DS_emd_remaining) = (u16)my_filelength(DSW(PS_DS_emd_file));
}

/* PS 26fc:004f emd_open(name) — "rb" DS:1E04 */
u16 emd_open(u16 name_ds)
{
    u16 f = my_fopen(name_ds, 0x1E04);
    if (!f) sd_fatal_msg(FS_EMD, 0x18, name_ds);
    emd_set_stream(f);
    return DSW(PS_DS_emd_remaining);
}

/* PS 26fc:0098 emd_read(near dst, size, n) — returns bytes remaining */
u16 emd_read(u16 dst_ds, s16 size, s16 n)
{
    u16 k = (u16)(size * n);
    if (DSW(PS_DS_emd_remaining) < k) sd_fatal(FS_EMD, 0x20);
    if (my_fread(dst_ds, (s16)k, 1, DSW(PS_DS_emd_file)) != 1) sd_fatal(FS_EMD, 0x22);
    DSW(PS_DS_emd_remaining) = (u16)(DSW(PS_DS_emd_remaining) - k);
    return DSW(PS_DS_emd_remaining);
}

/* PS 26fc:0119 emd_write (no callers) */
s16 emd_write(u16 src_ds, s16 size, s16 n)
{
    return my_fwrite(src_ds, (s16)(size * n), 1, DSW(PS_DS_emd_file));
}

/* PS 26fc:013c emd_read_u8 (no callers) */
u8 emd_read_u8(void)
{
    u16 m = stk_mark();
    u16 b = stk_alloc(2);
    emd_read(b, 1, 1);
    u8 v = DSB(b);
    stk_release(m);
    return v;
}

/* PS 26fc:015b emd_read_string (no callers): bytes into DS:419A until NUL, near strdup */
u16 emd_read_string(void)
{
    u16 p = PS_DS_emd_bounce;
    do emd_read(p, 1, 1); while (DSB(p++) != 0);
    u16 d = xmalloc((u16)(rtl_strlen(PS_DS_emd_bounce) + 1));
    return rtl_strcpy(d, PS_DS_emd_bounce);
}

/* PS 26fc:0197 emd_read_date(u16 far *out) — 3 bytes m/d/y into a stack local, date_make */
void emd_read_date(FarPtr out)
{
    u16 m = stk_mark();
    u16 loc = stk_alloc(4);
    emd_read_far(ds_ptr(loc), 1, 1);
    emd_read_far(ds_ptr((u16)(loc + 1)), 1, 1);
    emd_read_far(ds_ptr((u16)(loc + 2)), 1, 1);
    fww(out, 0, date_make(ds_ptr(loc)));
    stk_release(m);
}

/* PS 26fc:01eb emd_read_far(far dst, size, n) — through the 128-byte bounce buffer DS:419A */
u16 emd_read_far(FarPtr dst, s16 size, s16 n)
{
    u16 left = (u16)(size * n);
    while (left) {
        u16 k = left > 0x80 ? 0x80 : left;
        emd_read(PS_DS_emd_bounce, 1, (s16)k);
        for (s16 i = 0; i < (s16)k; i++) {
            fwb(dst, 0, DSB(PS_DS_emd_bounce + i));
            dst.off++;
        }
        left = (u16)(left - k);
    }
    return DSW(PS_DS_emd_remaining);
}

/* PS 26fc:0257 emd_write_far (no callers) */
void emd_write_far(FarPtr src, s16 size, s16 n)
{
    u16 left = (u16)(size * n);
    while (left) {
        u16 k = left > 0x80 ? 0x80 : left;
        for (s16 i = 0; i < (s16)k; i++) {
            DSB(PS_DS_emd_bounce + i) = fb(src, 0);
            src.off++;
        }
        emd_write(PS_DS_emd_bounce, 1, (s16)k);
        left = (u16)(left - k);
    }
}

/* PS 26fc:02c0 emd_close(keep_rest) — near buffer of the rest (or 0); asserts EOF */
FarPtr emd_close(s8 keep_rest)
{
    u16 buf = 0;
    if (keep_rest && DSW(PS_DS_emd_remaining)) {
        buf = xmalloc(DSW(PS_DS_emd_remaining));
        emd_read(buf, 1, (s16)DSW(PS_DS_emd_remaining));
    }
    if (!(my_fgetc(DSW(PS_DS_emd_file)) == -1 && DSW(PS_DS_emd_remaining) == 0)) sd_fatal(FS_EMD, 0x74);
    my_fclose(DSW(PS_DS_emd_file));
    return buf ? ds_ptr(buf) : far_null();
}

/* PS 26fc:0338 emd_close_rest_far — far block of the rest (or NULL); asserts EOF */
FarPtr emd_close_rest_far(void)
{
    FarPtr buf = far_null();
    if (DSW(PS_DS_emd_remaining)) {
        buf = xfarmalloc(DSW(PS_DS_emd_remaining));
        FarPtr q = buf;
        while (DSW(PS_DS_emd_remaining)) {
            u16 k = DSW(PS_DS_emd_remaining) > 0x80 ? 0x80 : DSW(PS_DS_emd_remaining);
            emd_read(PS_DS_emd_bounce, 1, (s16)k);
            for (s16 i = 0; i < (s16)k; i++) { fwb(q, 0, DSB(PS_DS_emd_bounce + i)); q.off++; }
        }
    }
    if (!(my_fgetc(DSW(PS_DS_emd_file)) == -1 && DSW(PS_DS_emd_remaining) == 0)) sd_fatal(FS_EMD, 0x8A);
    my_fclose(DSW(PS_DS_emd_file));
    return buf;
}

/* ================================================================================================
 * 273b list.c — counted arrays: +00 elsize, +02 count, +04 cursor, +06 far data
 * ================================================================================================ */

/* PS 273b:000e list_count (no callers) */
u16 list_count(FarPtr l) { return fw(l, 2); }

/* PS 273b:001c list_first */
FarPtr list_first(FarPtr l)
{
    fww(l, 4, 0);
    return fp(l, 6);
}

/* PS 273b:0034 list_next */
FarPtr ps_list_next(FarPtr l)
{
    fww(l, 4, (u16)(fw(l, 4) + 1));
    FarPtr d = fp(l, 6);
    if (far_is_null(d) || (s16)fw(l, 4) >= (s16)fw(l, 2)) return far_null();
    return far_add(d, (u16)(fw(l, 4) * fw(l, 0)));
}

/* PS 273b:0080 list_last (no empty check) */
FarPtr list_last(FarPtr l)
{
    fww(l, 4, (u16)(fw(l, 2) - 1));
    return far_add(fp(l, 6), (u16)(fw(l, 4) * fw(l, 0)));
}

/* PS 273b:00b6 list_at */
FarPtr list_at(FarPtr l, s16 i)
{
    FarPtr d = fp(l, 6);
    if (far_is_null(d) || i < 0 || i >= (s16)fw(l, 2)) sd_fatal(FS_LIST, 0x46);
    fww(l, 4, (u16)i);
    return far_add(d, (u16)(i * fw(l, 0)));
}

/* PS 273b:0121 list_find_dated — the element in force at date (NULL before the first) */
FarPtr list_find_dated(FarPtr l, u16 date)
{
    s16 n = (s16)fw(l, 2);
    FarPtr cur = fp(l, 6);
    if (far_is_null(cur) || fw(cur, 0) > date) return far_null();
    u16 es = fw(l, 0);
    FarPtr nxt = far_add(cur, es);
    s16 last = (s16)(n - 1), i;
    for (i = 0; i < last; i++) {
        if (fw(cur, 0) > fw(nxt, 0)) sd_fatal(FS_LIST, 0x69);
        if (fw(cur, 0) <= date && fw(nxt, 0) > date) {
            fww(l, 4, (u16)i);
            return cur;
        }
        cur = nxt;
        nxt = far_add(nxt, es);
    }
    fww(l, 4, (u16)last);
    return cur;
}

/* PS 273b:01f3 list_first_date (no callers) */
u16 list_first_date(FarPtr l)
{
    FarPtr d = fp(l, 6);
    if (far_is_null(d)) sd_fatal(FS_LIST, 0x81);
    fww(l, 4, 0);
    return fw(d, 0);
}

/* PS 273b:0249 list_dated_marker — empty; its address is the "dated elements" flag of list_read */
void list_dated_marker(void) {}

typedef void (*FarRecFn)(FarPtr rec);

/* PS 273b:024e list_read(l, elsize, rd) */
void list_read(FarPtr l, s16 elsize, FarPtr rd)
{
    rtl__fmemset(l, 0, 10);
    fww(l, 0, (u16)elsize);
    emd_read_far(far_add(l, 2), 2, 1);
    if (fw(l, 2) == 0) {
        fwp(l, 6, far_null());
        return;
    }
    FarPtr d = xfarmalloc((u32)(u16)(fw(l, 2) * (u16)elsize));
    fwp(l, 6, d);
    FarPtr marker = codeptr_far(PS_FN_list_dated_marker);
    for (s16 i = 0; i < (s16)fw(l, 2); i++) {
        if (far_eq(rd, marker)) {
            emd_read_date(d);
            emd_read_far(far_add(d, 2), (s16)(elsize - 2), 1);
        } else if (!far_is_null(rd)) {
            ((FarRecFn)codeptr_lookup(rd))(d);
        } else {
            emd_read_far(d, elsize, 1);
        }
        d.off = (u16)(d.off + (u16)elsize);
    }
}

/* PS 273b:0348 list_free(l, fr) */
void list_free(FarPtr l, FarPtr fr)
{
    FarPtr d = fp(l, 6);
    if (far_is_null(d)) return;
    if (!far_is_null(fr)) {
        FarRecFn f = (FarRecFn)codeptr_lookup(fr);
        FarPtr q = d;
        for (s16 i = 0; i < (s16)fw(l, 2); i++) {
            f(q);
            q.off = (u16)(q.off + fw(l, 0));
        }
    }
    xfarfree(fp(l, 6));
    rtl__fmemset(l, 0, 10);
}

/* ================================================================================================
 * 26c4 text.c — merged text tables u16 total; u16 offs[total]; text
 * ================================================================================================ */

/* PS 26c4:0005 text_read_whole (no callers) */
FarPtr text_read_whole(u16 name_ds)
{
    emd_open(name_ds);
    return emd_close_rest_far();
}

/* PS 26c4:001a text_free */
void text_free(FarPtr t) { xfarfree(t); }

/* PS 26c4:002c text_count */
u16 text_count(FarPtr t) { return fw(t, 0); }

/* PS 26c4:0039 text_get */
FarPtr text_get(FarPtr t, s16 i)
{
    s16 n = (s16)text_count(t);
    if (i < 0 || i >= n) sd_fatal(FS_TEXT, 0x20);
    u16 o = fw(t, (u16)((i + 1) * 2));
    t.off = (u16)(t.off + (u16)((n + 1) * 2));
    return far_add(t, o);
}

/* PS 26c4:00a5 extdb_text(db, id) = table_text(db, id, rec->u32 @+0) */
FarPtr extdb_text(s16 db, s16 id)
{
    FarPtr r = table_rec(db, id);
    return table_text(db, id, fl(r, 0));
}

/* PS 26c4:00de text_read_count(fp, i) — files_foreach callback */
static void text_read_count(u16 f, s16 i)
{
    if (f) my_fread((u16)(PS_DS_text_counts + i * 2), 2, 1, f);
    else DSW(PS_DS_text_counts + i * 2) = 0;
}

/* PS 26c4:0112 text_measure(fp, i) — files_foreach callback */
static void text_measure(u16 f, s16 i)
{
    if (f) DSW(PS_DS_text_sizes + i * 2) =
               (u16)((u16)my_filelength(f) - (u16)((DSW(PS_DS_text_counts + i * 2) + 1) * 2));
    else DSW(PS_DS_text_sizes + i * 2) = 0;
}

/* PS 26c4:014b text_scan_files(fp[9]) — highest i in 8..1 with a count (0 if none) */
static s16 text_scan_files(u16 fp9_ds)
{
    files_foreach(fp9_ds, codeptr_far(SD_FN(0x26c4, 0x00de)));
    files_foreach(fp9_ds, codeptr_far(SD_FN(0x26c4, 0x0112)));
    s16 i = 8;
    while (i > 0 && DSW(PS_DS_text_counts + i * 2) == 0) i--;
    return i;
}

/* PS 26c4:0190 text_rebase(far offs, i) — adds DS:5ACC to the n_i offsets */
static void text_rebase(FarPtr offs, s16 i)
{
    for (s16 k = 0; k < (s16)DSW(PS_DS_text_counts + i * 2); k++)
        fww(offs, (u16)(k * 2), (u16)(fw(offs, (u16)(k * 2)) + DSW(PS_DS_text_base)));
}

/* PS 26c4:01c5 text_read_file(fp, i) — files_foreach callback */
static void text_read_file(u16 f, s16 i)
{
    if (f) {
        ps_fread_far_1193(ds_far(PS_DS_text_offcur), 2, DSW(PS_DS_text_counts + i * 2), f);
        text_rebase(ds_far(PS_DS_text_offcur), i);
        ps_fread_far_1193(ds_far(PS_DS_text_textcur), 1, DSW(PS_DS_text_sizes + i * 2), f);
        u16 sz = DSW(PS_DS_text_sizes + i * 2);
        DSW(PS_DS_text_textcur) = (u16)(DSW(PS_DS_text_textcur) + sz);
        DSW(PS_DS_text_base) = (u16)(DSW(PS_DS_text_base) + sz);
    }
    u16 adv;
    if (!f) adv = DSW(PS_DS_text_stride);
    else if (i == 0 || DSW(PS_DS_text_stride) == 0) adv = DSW(PS_DS_text_counts + i * 2);
    else adv = DSW(PS_DS_text_stride);
    DSW(PS_DS_text_offcur) = (u16)(DSW(PS_DS_text_offcur) + adv * 2);
}

/* PS 26c4:0258 text_fill(far buf, fp[9]) */
static void text_fill(FarPtr buf, u16 fp9_ds)
{
    rtl__fmemcpy(buf, ds_ptr(PS_DS_text_total_count), 2);
    ds_far_wr(PS_DS_text_offcur, far_add(buf, 2));
    ds_far_wr(PS_DS_text_textcur, far_add(buf, (u16)((DSW(PS_DS_text_total_count) + 1) * 2)));
    DSW(PS_DS_text_base) = 0;
    files_foreach(fp9_ds, codeptr_far(SD_FN(0x26c4, 0x01c5)));
}

/* PS 26c4:02b3 sum_words(u16 *a, n) */
static u16 sum_words(u16 a_ds, s16 n)
{
    u16 s = 0;
    for (s16 i = 0; i < n; i++) s = (u16)(s + DSW(a_ds + i * 2));
    return s;
}

/* PS 26c4:02d9 text_load(name, ext, stride) */
FarPtr text_load(u16 name_ds, u16 ext_ds, s16 stride)
{
    u16 m = stk_mark();
    u16 fp9 = stk_alloc(18);
    DSW(PS_DS_text_stride) = (u16)stride;
    open_module_files(fp9, name_ds, ext_ds);
    s8 last = (s8)text_scan_files(fp9);
    if (stride) DSW(PS_DS_text_total_count) = (u16)(DSW(PS_DS_text_counts) + (s16)last * stride);
    else DSW(PS_DS_text_total_count) = sum_words(PS_DS_text_counts, 9);
    DSW(PS_DS_text_total_size) = sum_words(PS_DS_text_sizes, 9);
    u16 sz = (u16)((DSW(PS_DS_text_total_count) + 1) * 2 + DSW(PS_DS_text_total_size));
    FarPtr buf = xfarmalloc((u32)(s32)(s16)sz);
    rtl__fmemset(buf, 0, sz);
    text_fill(buf, fp9);
    files_close(fp9);
    stk_release(m);
    return buf;
}

/* files_foreach callbacks registered for their far pointers */
__attribute__((constructor)) static void lowlevel_codeptrs(void)
{
    codeptr_register(PROG_PS, PS_FN_list_dated_marker, list_dated_marker);
    codeptr_register(PROG_PS, SD_FN(0x26c4, 0x00de), (CodeFn)text_read_count);
    codeptr_register(PROG_PS, SD_FN(0x26c4, 0x0112), (CodeFn)text_measure);
    codeptr_register(PROG_PS, SD_FN(0x26c4, 0x01c5), (CodeFn)text_read_file);
}
