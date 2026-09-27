/* Picture, palette and font loaders (platform.md §7): INTRO 05da:070b/0980 (palettes), 4a8f..54ae
 * (fonts, plane bitmaps, screens), 0ead (bmp_load), 0ef8 (vqt_predecode), 0f35 (scr_load), 0f48
 * (bmp_mtx_load); PS 16e3/168a/3922/396d/39aa/39bd, AP 1d32/7115/7229/715b/6f97/6fe3/6f83/26db.
 * The three programs run the same code except for the near allocators (INTRO malloc, PS the
 * one-argument lib_realloc, AP safe_malloc), AP's huge read (4bb7:0000) and AP's bmp_info_planes
 * (one shared width/height when the INF chunk is short). */
#include "platform/rtl_priv.h"

#include "prog.h"
#include "sim_flow/sim_flow.h"
#include "video/video.h"

/* frame record (§7.1) */
#define FR_SEG  0
#define FR_OFF  2
#define FR_KIND 4
#define FR_W    6
#define FR_H    8

#define CHK_STATE 0x44    /* chunk_save buffer (43h bytes, word aligned) */

static int prog(void) { return prog_current_id(); }

/* ---- per-program library calls */
static u16 L_malloc(u16 n)
{
    switch (prog()) {
    case PROG_PS: return lib_realloc(n);             /* 0000:03c1, the one-argument safe malloc */
    case PROG_AP: return safe_malloc(n);             /* 0000:035c */
    default:      return rtl_malloc(n);              /* 0000:03b4 */
    }
}
static u16 L_calloc(u16 n, u16 size)
{
    switch (prog()) {
    case PROG_PS: return ps_lib_calloc(n, size);
    case PROG_AP: return ap_lib_calloc(n, size);
    default:      return intro_lib_calloc(n, size);
    }
}
static void L_free(u16 p) { rtl_free(p); }
static s32 L_hread(FarPtr buf, s32 size, s32 n, u16 f)
{
    if (prog() == PROG_AP) return ap_fread_far_4bb7(buf, size, n, f);
    return my_hread(buf, size, n, f);
}

/* AP 4bb7:0000 fread_far(dst, size, count, f): my_fread in 20-byte pieces through a stack buffer, the
 * destination OFFSET advanced (no normalisation: a read past 64 KB wraps in the segment, as the
 * original); returns the bytes read. PORT: modules.csv assigns it to sim_flow (messages.c); it lives
 * here because the AP picture loaders need it (sim_flow should not define it again). */
s32 ap_fread_far_4bb7(FarPtr dst, s32 size, s32 count, u16 f_ds)
{
    u16 m = stk_mark();
    u16 tmp = stk_alloc(0x14);
    s32 left = size * count, got = 0;
    for (;;) {
        u16 n = left < 0x14 ? (u16)left : 0x14;
        if (!n) break;
        got += (u16)my_fread(tmp, 1, (s16)n, f_ds);
        for (u16 i = 0; i < n; i++) wr8(dst.seg, (u16)(dst.off + i), DSB((u16)(tmp + i)));   /* _fmemcpy */
        dst.off = (u16)(dst.off + n);
        left -= (s16)n;
    }
    stk_release(m);
    return got;
}

static bool found(s32 r) { return r != -1; }
static FarPtr pada(FarPtr p, u32 n) { return far_norm(far_lin(p) + n); }   /* PADA@ / PADD@ */
static FarPtr psba(FarPtr p, u32 n) { return far_norm(far_lin(p) - n); }   /* PSBA@ */

static bool vga(void) { return LB(G_drv_dac_flag) != 0; }

/* the driver's entry 13: pixel-block bytes of a bitmap (+ a temporary size at *out) */
u32 drv_call_bm_size(u16 bmp_ds, u16 out_ds) { return ((DrvBmSizeFn)drv_entry(13))(bmp_ds, out_ds); }

/* ======================================================================= palettes (§7.6) */

static u16 pal_slots(void) { return lds(G_pal_slot); }

/* INTRO 05da:070b / PS 16e3:0d1f / AP 1d32:3e0b pal_load(x) -> far palette (0 on failure) */
u32 pal_load(u16 x)
{
    u16 m = stk_mark();
    FarPtr p = far_make(0, 0);
    bool opened = false;
    s8 ad = (s8)LB(G_drv_adapter);
    LW(G_pal_size) = DSW((u16)(lds(G_pal_size_tab) + 2 * ad));
    s16 i = 1;
    while (!far_is_null(ds_far((u16)(pal_slots() + 4 * i))) && i < 10) i++;
    if (i < 10) {
        if (!chunk_is_open(x)) { opened = true; x = chunk_open(x); }
        if (found(chunk_find(x, DSW((u16)(lds(G_pal_tag_tab) + 2 * ad)), 0))) {
            p = dos_alloc(LW(G_pal_size), 0, 0);
            if (!far_is_null(p)) {
                u16 buf = stk_alloc(0x300);
                my_fread(buf, 1, (s16)LW(G_pal_size), x);
                far_memmove_fp(p, ds_ptr(buf), (u32)(s32)(s16)LW(G_pal_size));
            }
        } else if (vga() && found(chunk_find(x, lds(G_pal_tag_amg), 0))) {
            u16 amg = stk_alloc(0x40);
            if (my_fread(amg, 1, 0x40, x) && !far_is_null(p = dos_alloc(LW(G_pal_size), 0, 0))) {
                FarPtr q = p;
                for (int k = 0; k < 32; k++) {
                    s16 c = DSS((u16)(amg + 2 * k));
                    wr8(q.seg, q.off++, (u8)(((c >> 8) & 0x0F) << 2));
                    wr8(q.seg, q.off++, (u8)(((c >> 4) & 0x0F) << 2));
                    wr8(q.seg, q.off++, (u8)((c & 0x0F) << 2));
                }
                for (int k = 0; k < 0x2A0; k++) wr8(q.seg, q.off++, 0);
            }
        }
        if (opened) chunk_close(x);
    }
    ds_far_wr((u16)(pal_slots() + 4 * i), p);        /* no free slot: writes slot 10 (original) */
    stk_release(m);
    return (u32)p.seg << 16 | p.off;
}

/* INTRO 05da:0980 / PS 16e3:0f94 / AP 1d32:4080 pal_free(p): every slot 1..9 holding p */
void pal_free(FarPtr pal)
{
    if (far_is_null(pal)) return;
    for (int i = 1; i < 10; i++) {
        u16 s = (u16)(pal_slots() + 4 * i);
        FarPtr q = ds_far(s);
        if (q.seg == pal.seg && q.off == pal.off) {
            dos_free(q);
            ds_far_wr(s, far_make(0, 0));
        }
    }
}

/* ========================================================================== fonts (§7.7) */

static u16 f_ptr(int i) { return (u16)(lds(G_font_glyphs) + 4 * i); }
static u16 f_ofs(int i) { return (u16)(lds(G_font_ofs) + 4 * i); }
static u16 f_wid(int i) { return (u16)(lds(G_font_wid) + 4 * i); }
#define F_B(g, i) DSB((u16)(lds(g) + (i)))

/* INTRO 05da:4a8f / PS 16e3:4db3 / AP 7115:0000 font_load(x) -> slot 2..19, 0 on failure */
u16 font_load(u16 x)
{
    u16 m = stk_mark();
    s16 i = 2;
    while (!far_is_null(ds_far(f_ptr(i))) && i < 20) i++;
    if (i >= 20) { stk_release(m); return 0; }
    bool opened = false;
    if (!chunk_is_open(x)) { opened = true; x = chunk_open(x); }
    if (!found(chunk_find(x, ds_lit("FNT:"), 0))) { i = 0; goto done; }
    my_fread((u16)(lds(G_font_w) + i), 1, 1, x);
    u8 b = F_B(G_font_w, i);
    if (b == 0xFD || b == 0xFF) {
        F_B(G_font_kind, i) = (u8)-b;
        my_fread((u16)(lds(G_font_w) + i), 1, 1, x);
        my_fread((u16)(lds(G_font_h) + i), 1, 1, x);
        my_fread((u16)(lds(G_font_uline) + i), 1, 1, x);
        my_fread((u16)(lds(G_font_first) + i), 1, 1, x);
        my_fread((u16)(lds(G_font_count) + i), 1, 1, x);
        u16 psize = stk_alloc(2);
        my_fread(psize, 2, 1, x);
        u16 size = DSW(psize);
        /* PORT: the block pointer is an uninitialised local when pk_open fails; 0 here */
        FarPtr blk = far_make(0, 0);
        s16 h = pk_open(-1, x, ds_lit("r"), (u32)chunk_size(x));
        bool fail = h < 0;
        if (!fail) fail = (u16)pk_size(h) != size;
        if (!fail) fail = far_is_null(blk = dos_alloc(size, 0, 0));
        if (!fail) fail = pk_read(h, blk, size) != size;
        if (!fail) {
            u16 cnt = F_B(G_font_count, i);
            ds_far_wr(f_ofs(i), blk);
            blk.off = (u16)(blk.off + 2 * cnt);
            ds_far_wr(f_wid(i), blk);
            blk.off = (u16)(blk.off + cnt);
            ds_far_wr(f_ptr(i), blk);
        }
        pk_close(h);
        if (fail) {
            if (!far_is_null(blk)) dos_free(blk);
            i = 0;
        }
    } else {
        u16 bpr;
        if (b == 0xFE) {
            F_B(G_font_kind, i) = 2;
            my_fread((u16)(lds(G_font_w) + i), 1, 1, x);
            bpr = F_B(G_font_w, i);
        } else {
            F_B(G_font_kind, i) = 0;
            bpr = (u16)((b + 7) >> 3);
        }
        my_fread((u16)(lds(G_font_h) + i), 1, 1, x);
        my_fread((u16)(lds(G_font_first) + i), 1, 1, x);
        my_fread((u16)(lds(G_font_count) + i), 1, 1, x);
        u16 n = (u16)((u16)(F_B(G_font_h, i) * F_B(G_font_count, i)) * bpr);
        u16 p = L_malloc(n);
        if (p) {
            my_fread(p, (s16)n, 1, x);
            ds_far_wr(f_ptr(i), ds_ptr(p));
            ds_far_wr(f_ofs(i), far_make(0, 0));
            ds_far_wr(f_wid(i), far_make(0, 0));
        } else {
            i = 0;
        }
    }
done:
    if (opened) chunk_close(x);
    stk_release(m);
    return (u16)i;
}

/* INTRO 05da:4e01 / PS 16e3:5125 / AP 7115:0372 font_free(slot) */
void font_free(u16 slot)
{
    s16 i = (s16)slot;
    if (!gfx_font_valid(i)) return;
    FarPtr p = ds_far(f_ptr(i)), p0 = ds_far(f_ptr(0));
    if (p.seg == p0.seg && p.off == p0.off) {
        F_B(G_font_kind, 0) = 0;
        F_B(G_font_count, 0) = 0; F_B(G_font_first, 0) = 0;
        F_B(G_font_uline, 0) = 0; F_B(G_font_h, 0) = 0; F_B(G_font_w, 0) = 0;
        ds_far_wr(f_ofs(0), far_make(0, 0));
        ds_far_wr(f_wid(0), far_make(0, 0));
        ds_far_wr(f_ptr(0), far_make(0, 0));
    }
    FarPtr o = ds_far(f_ofs(i));
    if (!far_is_null(o)) dos_free(o);
    else L_free(ds_far(f_ptr(i)).off);
    F_B(G_font_kind, i) = 0;
    ds_far_wr(f_ptr(i), far_make(0, 0));
    ds_far_wr(f_ofs(i), far_make(0, 0));
    ds_far_wr(f_wid(i), far_make(0, 0));
}

/* ================================================================== bitmap objects (§7.1) */

/* INTRO 05da:539d / PS 168a:048f / AP 7229:0546 bmp_free_table: free(bmp[0]); free(bmp) */
void bmp_free_table(u16 p0_ds)
{
    if (DSW(p0_ds)) L_free(DSW(p0_ds));
    if (p0_ds) L_free(p0_ds);
}

/* INTRO 05da:53c1 / PS 168a:04b1 / AP 7229:056a bmp_free: the shared pixel block, then the table */
void bmp_free(u16 p0_ds)
{
    if (!p0_ds) return;
    u16 fr = DSW(p0_ds);
    dos_free(far_make(DSW((u16)(fr + FR_SEG)), DSW((u16)(fr + FR_OFF))));
    bmp_free_table(p0_ds);
}

static s16 count_frames(u16 bmp)
{
    s16 n = 0;
    if (bmp) while (DSW((u16)(bmp + 2 * n))) n++;
    return n;
}

/* INTRO 05da:53ef / PS 39a1:0010 / AP 26d2:000f bmp_count */
s16 bmp_count(u16 p0_ds) { return count_frames(p0_ds); }
/* INTRO 0f2c:0010 (the copy bmp_load calls) */
u16 intro_bmp_count(u16 a0) { return (u16)count_frames(a0); }
/* PS 168a:04dc */
s16 ps_bmp_count(u16 p0_ds) { return count_frames(p0_ds); }
/* AP 7229:0598 */
s16 ap_bmp_count(u16 p0_ds) { return count_frames(p0_ds); }

/* ============================================================ nibble-plane bitmaps (§7.4) */

/* INTRO 05da:4ee4 / PS 168a:0000 bmp_info_planes(f, &n, &bmp): n widths, then n heights */
static s16 info_planes_ip(u16 f, u16 pn, u16 pb)
{
    if (!found(chunk_find(f, ds_lit("BMP:INF:"), 0))) return 0;
    if (my_fread(pn, 2, 1, f) != 1) return 0;
    s16 n = DSS(pn);
    DSW(pb) = L_calloc((u16)((n + 1) * 2), 1);
    if (!DSW(pb)) return 0;
    u16 t = DSW(pb);
    DSW(t) = L_calloc((u16)n, 10);
    if (!DSW(t)) return 0;
    s16 i;
    for (i = 0; DSS(pn) > i;) {
        if (my_fread((u16)(DSW((u16)(t + 2 * i)) + FR_W), 2, 1, f) != 1) return 0;
        i++;
        DSW((u16)(t + 2 * i)) = (u16)(DSW(t) + 10 * i);
    }
    for (i = 0; DSS(pn) > i; i++)
        if (my_fread((u16)(DSW((u16)(t + 2 * i)) + FR_H), 2, 1, f) != 1) return 0;
    DSW((u16)(t + 2 * DSS(pn))) = 0;
    return 1;
}

/* AP 7229:0000 bmp_info_planes: one read of all widths and heights; a short INF chunk (fewer than
 * 4n bytes after the count) holds one width/height for every frame; frees what it allocated */
static s16 info_planes_ap(u16 f, u16 pn, u16 pb)
{
    u16 tmp = 0;
    DSW(pb) = 0;
    if (!found(chunk_find(f, ds_lit("BMP:INF:"), 0))) return 0;
    if (my_fread(pn, 2, 1, f) != 1) return 0;
    s16 n = DSS(pn);
    if (!(DSW(pb) = L_calloc((u16)((n + 1) * 2), 1))) goto fail;
    if (!(DSW(DSW(pb)) = L_calloc((u16)n, 10))) goto fail;
    s16 m = (chunk_size(f) - 2 < (s32)(s16)(n * 4)) ? 1 : n;
    if (!(tmp = L_malloc((u16)(m * 4)))) goto fail;
    if (my_fread(tmp, (s16)(m * 4), 1, f) != 1) goto fail;
    u16 W = tmp, H = (u16)(tmp + 2 * m), rec = DSW(DSW(pb)), t = DSW(pb);
    for (s16 i = 0; n > i; i++) {
        DSW(t) = rec;
        DSW((u16)(rec + FR_W)) = DSW(W);
        DSW((u16)(rec + FR_H)) = DSW(H);
        if (n == m) { W += 2; H += 2; }
        rec += 10;
        t += 2;
    }
    DSW(t) = 0;
    L_free(tmp);
    return 1;
fail:
    if (tmp) L_free(tmp);
    if (DSW(pb)) {
        if (DSW(DSW(pb))) L_free(DSW(DSW(pb)));
        L_free(DSW(pb));
        DSW(pb) = 0;                                  /* PORT: the original leaves it dangling (the
                                                         caller would free it a second time) */
    }
    return 0;
}

static s16 bmp_info_planes(u16 f, u16 pn, u16 pb)
{
    return prog() == PROG_AP ? info_planes_ap(f, pn, pb) : info_planes_ip(f, pn, pb);
}

/* INTRO 05da:540f / PS 168a:04fe / AP 7229:05b8 amiga_to_nibbles(src, dst, n): backwards, 4 bytes
 * per source byte (even bits -> 0/1, odd bits -> OR 10h) */
static void amiga_to_nibbles(FarPtr src, FarPtr dst, u16 n)
{
    src = pada(src, (u16)(n - 1));
    dst = pada(dst, (u16)(n * 4 - 1));
    while (n-- != 0) {
        u8 c = rd8(src.seg, src.off);
        src = psba(src, 1);
        for (u16 bit = 1; bit & 0xFF; bit <<= 1) {
            if (bit & 0xAA) {
                wr8(dst.seg, dst.off, (u8)(rd8(dst.seg, dst.off) | ((c & bit) ? 0x10 : 0)));
                dst = psba(dst, 1);
            } else {
                wr8(dst.seg, dst.off, (c & bit) ? 1 : 0);
            }
        }
    }
}

/* INTRO 05da:5001 / PS 168a:011c / AP 7229:01aa bmp_load_planes(x) */
u16 bmp_load_planes(u16 x)
{
    u16 m = stk_mark();
    u16 pb = stk_alloc(2), pn = stk_alloc(2), ptmp = stk_alloc(2), r = ds_lit("r");
    u16 scr = lds(G_pk_shared_scratch);
    DSW(pb) = 0;
    s16 ok = 0, h = 0;
    bool opened = false;
    FarPtr pix = far_make(0, 0), tmp = far_make(0, 0);
    u16 scratch_p = 0;
    u32 size;
    if (!chunk_is_open(x)) { opened = true; x = chunk_open(x); }   /* (failure not checked: JAE) */
    if (!bmp_info_planes(x, pn, pb)) goto out;
    size = drv_call_bm_size(DSW(pb), ptmp);
    pix = dos_alloc(size, 0, 0);
    if (far_is_null(pix)) goto out;
    if (DSW(ptmp)) {
        tmp = dos_alloc((u32)(s32)DSS(ptmp), 0, 0);
        if (far_is_null(tmp)) goto out;
    }
    if (far_is_null(ds_far(scr))) {                   /* the LZW scratch, paragraph aligned */
        scratch_p = L_malloc(0x3CC4);
        if (scratch_p) {
            L_free(scratch_p);
            scratch_p = L_malloc(0x3AC4);
            if (scratch_p) {
                FarPtr s = pada(ds_ptr(scratch_p), 0x10);
                s.off &= 0xFFF0;
                ds_far_wr(scr, huge_norm(s));
            }
        }
    }
    if (!found(chunk_find(x, ds_lit("BMP:BIN:"), 0))) goto out;
    h = pk_open(0, x, r, (u32)chunk_size(x));
    if (h < 0) goto out;
    FarPtr cur = pix;
    while (pk_read(h, cur, 0x7FFF) == 0x7FFF) cur = pada(cur, 0x7FFF);
    /* entry 14 (bmp, pix, unpacked size, tmp, pixel bytes): the driver's layout of the low nibbles */
    ((DrvBmLayoutFn)drv_entry(14))(DSW(pb), pix, (u32)pk_size(h));
    pk_close(h);
    ok = 1;
    if (!vga()) goto out;
    if (found(chunk_find(x, ds_lit("BMP:VGA:"), 0))) ok = 5;
    else if (found(chunk_find(x, ds_lit("BMP:AMG:"), 0))) ok = 6;
    if (ok < 5) goto out;
    h = pk_open(0, x, r, (u32)chunk_size(x));
    if (h < 0) goto out;
    s32 len = 0x7FFF;
    while (far_is_null(tmp = dos_alloc((u32)len, 0, 0))) len >>= 1;   /* (the DRV13 temp leaks) */
    cur = pix;
    for (;;) {
        s16 k = (s16)pk_read(h, tmp, (u16)len);
        if (k <= 0) break;
        if (ok == 6) { amiga_to_nibbles(tmp, tmp, (u16)k); k = (s16)(k << 2); }
        ((DrvBmMergeFn)drv_entry(15))(tmp, cur, (u32)(u16)k);
        /* PORT: INTRO and AP pass the high word of 2*len as the low word to PADA@ (a compiler
         * slip; CX is left over from the driver call); PS's correct 2*len is used for all */
        cur = pada(cur, (u32)len * 2);
    }
    pk_close(h);
out:
    if (!far_is_null(tmp)) dos_free(tmp);
    if (scratch_p) {
        L_free(scratch_p);
        ds_far_wr(scr, far_make(0, 0));
    }
    if (!ok) {
        if (!far_is_null(pix)) dos_free(pix);
        if (h) pk_close(h);
        if (DSW(pb)) bmp_free_table(DSW(pb));         /* PORT: (the original also calls it with 0,
                                                         which reads DS:0000 = 0) */
        DSW(pb) = 0;
    }
    if (opened) chunk_close(x);
    u16 b = DSW(pb);
    stk_release(m);
    return b;
}

/* ============================================================= screens (§7.5) */

/* INTRO 05da:54ae / PS 16e3:5208 / AP 715b:0000 scr_load_planes(x) -> 0 / 1 / 5 / 6 */
u16 scr_load_planes(u16 a0)
{
    u16 m = stk_mark();
    u16 pw = stk_alloc(2), ph = stk_alloc(2), r = ds_lit("r");
    DSW(pw) = 0x140;
    DSW(ph) = 0xC8;
    gfx_ega_restore();
    s16 ret = 0, h = -1;
    u16 x = a0, buf = 0;
    bool opened = false;
    if (!chunk_is_open(x)) { opened = true; x = chunk_open(x); }
    if (found(chunk_find(x, ds_lit("SCR:DIM:"), 0))) {
        my_fread(pw, 2, 1, x);
        my_fread(ph, 2, 1, x);
    }
    s16 hgt = DSS(ph);
    if (!found(chunk_find(x, ds_lit("SCR:BIN:"), 0))) goto close;
    h = pk_open(0, x, r, (u32)chunk_size(x));
    if (h < 0) goto close;
    s16 half = (s16)(DSS(pw) >> 1);
    u16 bs = (u16)(half << 7);
    for (;;) {
        buf = L_malloc(bs);
        if (buf) break;
        bs >>= 1;
        if (bs < (u16)half) break;
    }
    if (!buf) goto closepk;
    s16 y = 0, rows = (s16)(bs / (u16)half);
    u16 n = bs;
    while (y < hgt) {
        pk_read(h, ds_ptr(buf), n);
        gfx_nib_put(ds_ptr(buf), 0, (u16)y, (u16)(half * 2), (u16)rows);
        y = (s16)(y + rows);
        if ((s16)(y + rows) > hgt) { rows = (s16)(hgt - y); n = (u16)(rows * half); }
    }
    ret = 1;
    if (!vga()) goto freebuf;
    pk_close(h);
    if (found(chunk_find(x, ds_lit("SCR:VGA:"), 0))) ret = 5;
    else if (found(chunk_find(x, ds_lit("SCR:AMG:"), 0))) ret = 6;
    if (ret < 5) goto freebuf;                        /* (h is closed a second time: harmless) */
    h = pk_open(0, x, r, (u32)chunk_size(x));
    if (h < 0) goto freebuf;
    y = 0;
    rows = (s16)(bs / (u16)half);
    if (ret == 6) bs >>= 2;
    n = bs;
    while (y < hgt) {
        pk_read(h, ds_ptr(buf), n);
        if (ret == 6) amiga_to_nibbles(ds_ptr(buf), ds_ptr(buf), n);
        gfx_nib_or_hi(ds_ptr(buf), 0, (u16)y, (u16)(half * 2), (u16)rows);
        y = (s16)(y + rows);
        if ((s16)(y + rows) > hgt) {
            rows = (s16)(hgt - y);
            n = (u16)(rows * half);
            if (ret == 6) n >>= 2;
        }
    }
freebuf:
    L_free(buf);
closepk:
    pk_close(h);
close:
    if (opened) chunk_close(x);
    stk_release(m);
    return (u16)ret;
}

/* INTRO 0f35:000a / PS 39aa:000a / AP 6f83:0000 scr_load(x): VQT screens decode straight to the
 * drawing page; the rest go through scr_load_planes. Returns 0 / -1 / the planes result (dropped:
 * no caller uses it). The second word is unused. */
void scr_load(u16 name_ds, u16 unused)
{
    (void)unused;
    u16 f = name_ds, m = stk_mark();
    u16 st = stk_alloc(CHK_STATE);
    FarPtr buf = far_make(0, 0);
    bool opened = false;
    if (!chunk_is_open(f)) {
        opened = true;
        f = chunk_open(f);
        if (!f) goto done;
    }
    chunk_save(st, f);
    if (!found(chunk_find(f, ds_lit("SCR:VQT:"), 0))) {
        chunk_restore(st);
        scr_load_planes(f);
        goto done;
    }
    u32 size = (u32)chunk_size(f);
    buf = dos_alloc(size, 0, 0);
    if (far_is_null(buf)) goto done;
    L_hread(buf, (s32)size, 1, f);
    LW(G_vqt_ctx) = vqt_begin(buf);
    if (LW(G_vqt_ctx)) {
        vqt_scr_node(0, 0, 0x140, 0xC8);
        vqt_end();
    }
done:
    if (opened) chunk_close(f);
    if (!far_is_null(buf)) dos_free(buf);
    stk_release(m);
}

/* ================================================================== bmp_load (§7.2) */

/* INTRO 0ead:036c / PS 3922:036c / AP 6f97:036a bmp_set_kind(kind, bmp) */
static void bmp_set_kind(s16 kind, u16 bmp)
{
    for (u16 s = bmp; DSW(s); s += 2) DSS((u16)(DSW(s) + FR_KIND)) = kind;
}

/* INTRO 0ead:0387 / PS 3922:0387 / AP 6f97:0385 bmp_info_list(f, &n, &bmp) */
static s16 bmp_info_list(u16 f, u16 pn, u16 pb)
{
    if (!found(chunk_find(f, ds_lit("BMP:INF:"), 0))) return 0;
    if (my_fread(pn, 2, 1, f) != 1) return 0;
    if (!(DSW(pb) = L_calloc((u16)((DSS(pn) + 1) * 2), 1))) return 0;
    if (!(DSW(DSW(pb)) = L_calloc(DSW(pn), 10))) return 0;
    u16 sz = (u16)(DSW(pn) * 4);
    u16 tmp = L_malloc(sz);
    if (!tmp) return 0;
    if (my_fread(tmp, (s16)sz, 1, f) != 1) return 0;
    u16 W = tmp, H = (u16)(tmp + 2 * DSW(pn)), rec = DSW(DSW(pb)), t = DSW(pb);
    for (s16 i = 0; DSS(pn) > i; i++) {
        DSW(t) = rec;
        DSW((u16)(rec + FR_W)) = DSW(W);
        DSW((u16)(rec + FR_H)) = DSW(H);
        W += 2; H += 2; rec += 10; t += 2;
    }
    DSW(t) = 0;
    L_free(tmp);
    return 1;
}

/* INTRO 0ef8:000e / PS 396d:000e / AP 6fe3:0000 vqt_predecode(f, bmp): decode sequential VQT frames
 * into raw 8-bit pixels (four planes per frame) with the video decoder in memory mode */
static void vqt_predecode(u16 f, u16 bmp)
{
    u16 m = stk_mark();
    u16 ctx = stk_alloc(0x1CA);
    u16 maxneed = 0;
    for (u16 s = bmp; DSW(s); s += 2) {
        u16 fr = DSW(s);
        u16 need = (u16)gfx_rect_size(DSW((u16)(fr + FR_W)), DSW((u16)(fr + FR_H)));
        if (maxneed < need) maxneed = need;
    }
    FarPtr q = dos_alloc(0xFFFFFFFFu, 0, 0);
    s32 bufsize = (s32)((u32)q.seg << 16 | q.off);
    s32 remain = chunk_size(f);
    FarPtr buf = far_make(0, 0);
    if (remain <= bufsize) { bufsize = remain; maxneed = 0; }
    if ((s32)maxneed <= bufsize) buf = dos_alloc((u32)bufsize, 0, 0);
    if (far_is_null(buf)) {
        FarPtr sc = ds_far(lds(G_pk_shared_scratch));
        if (far_is_null(sc) || maxneed > 0x3AB4) { stk_release(m); return; }
        buf = sc;
        bufsize = 0x3AB4;
    }
    LW(G_vqt_ctx) = ctx;
    DSL(ctx) = 0;                                     /* bit position */
    ds_far_wr((u16)(ctx + 4), buf);                   /* source */
    L_hread(buf, bufsize, 1, f);
    remain -= bufsize;
    for (u16 s = bmp; DSW(s); s += 2) {
        u16 fr = DSW(s);
        u16 w = DSW((u16)(fr + FR_W)), hh = DSW((u16)(fr + FR_H));
        FarPtr p = far_make((u16)(DSW((u16)(fr + FR_SEG)) + (DSW((u16)(fr + FR_OFF)) >> 4)),
                            (u16)(DSW((u16)(fr + FR_OFF)) & 0x0F));
        u16 quarter = (u16)((s16)(w * hh) >> 2);
        for (int k = 0; k < 4; k++) {
            ds_far_wr((u16)(LW(G_vqt_ctx) + 8 + 4 * k), p);
            p.off = (u16)(p.off + quarter);
        }
        u16 yo = 0;
        for (s16 y = 0; (s16)hh > y; y++) {
            DSW((u16)(LW(G_vqt_ctx) + 0x18 + 2 * y)) = yo;
            yo = (u16)(yo + w);
        }
        vqt_mem_node(0, 0, w, hh);
        u16 c = LW(G_vqt_ctx);
        u16 used = (u16)((DSL(c) + 7) >> 3);
        DSL(c) = 0;
        FarPtr src = ds_far((u16)(c + 4));
        if (remain != 0) {
            FarPtr tail = pada(src, used);
            far_copy(src, tail, (u16)((u16)bufsize - used));
            FarPtr dst = pada(src, (u32)(bufsize - used));
            s32 n = (s32)used < remain ? (s32)used : remain;
            if (n > bufsize) n = bufsize;
            L_hread(dst, n, 1, f);
            remain -= n;
        } else {
            ds_far_wr((u16)(c + 4), pada(src, used));
        }
    }
    FarPtr sc = ds_far(lds(G_pk_shared_scratch));
    if (buf.seg != sc.seg || buf.off != sc.off) dos_free(buf);
    stk_release(m);
}

/* INTRO 0ead:0002 / PS 3922:0002 / AP 6f97:0000 bmp_load(x) */
u16 bmp_load(u16 x)
{
    u16 m = stk_mark();
    u16 s0 = stk_alloc(CHK_STATE), s1 = stk_alloc(CHK_STATE);
    u16 pb = stk_alloc(2), pn = stk_alloc(2), pfirst = stk_alloc(2), poff = stk_alloc(4), ptmp = stk_alloc(2);
    DSW(pb) = 0;
    FarPtr buf = far_make(0, 0);
    bool opened = false;
    u16 f = x;
    if (!chunk_is_open(f)) {
        opened = true;
        f = chunk_open(f);
        if (!f) goto fail;
    }
    chunk_save(s0, f);
    if (found(chunk_find(f, ds_lit("BMP:SCN:"), 0))) {
        chunk_save(s1, f);
        chunk_restore(s0);
        if (!bmp_info_list(f, pn, pb)) goto fail;
        bmp_set_kind(-2, DSW(pb));
        chunk_restore(s1);
        DSW(pfirst) = 0;
    } else if (found(chunk_find(f, ds_lit("BMP:OFF:"), 0))) {
        my_fread(pfirst, 2, 1, f);
        chunk_restore(s0);
        if (!bmp_info_list(f, pn, pb)) goto fail;
        bmp_set_kind(-1, DSW(pb));
        if (!found(chunk_find(f, ds_lit("BMP:VQT:"), 0))) goto fail;
    } else {
        DSW(pb) = bmp_load_planes(f);
        goto finish;
    }
    s16 n = DSS(pn);
    if (DSW(pfirst) == 0) {                          /* explicit offset list */
        s32 size = chunk_size(f);
        buf = dos_alloc((u32)size, 0, 0);
        if (far_is_null(buf)) goto fail;
        L_hread(buf, size, 1, f);
        if (!found(chunk_find(f, ds_lit("BMP:OFF:"), 0))) { dos_free(buf); goto fail; }
        for (s16 i = 0; i < n; i++) {
            if (my_fread(poff, 4, 1, f) != 1) { dos_free(buf); goto fail; }
            FarPtr p = pada(buf, DSL(poff));
            u16 fr = DSW((u16)(DSW(pb) + 2 * i));
            DSW((u16)(fr + FR_SEG)) = p.seg;
            DSW((u16)(fr + FR_OFF)) = p.off;
        }
    } else {                                          /* sequential VQT frames: pre-decode */
        u32 size = drv_call_bm_size(DSW(pb), ptmp);
        buf = dos_alloc(size, 0, 0);
        if (far_is_null(buf)) goto fail;
        bmp_set_kind(-4, DSW(pb));
        FarPtr p = buf;
        for (s16 i = 0; i < n; i++) {
            u16 fr = DSW((u16)(DSW(pb) + 2 * i));
            DSW((u16)(fr + FR_SEG)) = p.seg;
            DSW((u16)(fr + FR_OFF)) = p.off;
            p = pada(p, (u16)(DSW((u16)(fr + FR_W)) * DSW((u16)(fr + FR_H))));
        }
        vqt_predecode(f, DSW(pb));
    }
finish:
    n = count_frames(DSW(pb));
    for (s16 i = 0; i < n; i++) {
        u16 fr = DSW((u16)(DSW(pb) + 2 * i));
        DSW((u16)(fr + FR_SEG)) = (u16)(DSW((u16)(fr + FR_SEG)) + (DSW((u16)(fr + FR_OFF)) >> 4));
        DSW((u16)(fr + FR_OFF)) &= 0x0F;
    }
    if (found(chunk_find(f, ds_lit("BMP:RLE:"), 0))) bmp_to_scn(DSW(pb), 0x10);
    if (found(chunk_find(f, ds_lit("BMP:SCL:"), 0))) bmp_set_kind(-3, DSW(pb));
    goto close;
fail:
    bmp_free(DSW(pb));                                /* (0f2c:0002) */
    DSW(pb) = 0;
close:
    if (opened) chunk_close(f);
    u16 b = DSW(pb);
    stk_release(m);
    return b;
}

/* ================================================================ bmp_mtx_load (§7.8) */

#define MTX(g, ap) lds(g)      /* (the AP offsets, from 26db:0009, are symbols.csv rows now) */

/* INTRO 0f48:000c / PS 39bd:000c / AP 26db:0009 bmp_mtx_load(x): tile bitmap + BMP:MTX: matrix */
u16 bmp_mtx_load(u16 a0)
{
    u8 ad = LB(G_drv_adapter);
    if (ad == 8 || ad == 2) ((DrvWord1Fn)drv_entry(32))(0);
    u16 bmp = bmp_load(a0);
    if (!bmp) return 0;
    u16 frame = MTX(G_mtx_frame, 0x2224), th_g = MTX(G_mtx_th, 0x6418), tw_g = MTX(G_mtx_tw, 0x6416);
    DSW(MTX(G_mtx_scroll_x, 0x222E)) = 0;
    DSW(MTX(G_mtx_scroll_y, 0x2230)) = 0;
    u16 fr0 = DSW(bmp);
    DSW(tw_g) = DSW((u16)(fr0 + FR_W));
    DSW((u16)(frame + 6)) = DSW(tw_g);
    DSW(th_g) = DSW((u16)(fr0 + FR_H));
    DSW((u16)(frame + 8)) = DSW(th_g);
    DSW(MTX(G_mtx_lim_x, 0x6412)) = (u16)(0x140 - DSW(tw_g));
    DSW(MTX(G_mtx_lim_y, 0x6414)) = (u16)(0xC8 - DSW(th_g));
    u16 cnt = (u16)count_frames(bmp);
    u16 b2 = (u16)gfx_cache_upload(bmp);
    if (!b2) return 0;
    DSW(frame) = DSW(DSW(b2));
    bmp_free_table(b2);
    u16 offs = MTX(G_mtx_offsets, 0x2222);
    if (DSW(offs)) L_free(DSW(offs));
    DSW(offs) = L_malloc((u16)(cnt * 2));
    if (!DSW(offs)) return 0;
    u16 p = DSW(offs), acc = 0;
    for (u16 i = 0; i < cnt; i++) {
        DSW(p) = acc;
        p += 2;
        switch (ad) {
        case 1: case 2: acc = (u16)(acc + DSW(th_g)); break;
        case 3:         acc = (u16)(acc + (DSW(th_g) << 2)); break;
        case 8:         acc = (u16)(acc + (DSW(th_g) << 1)); break;
        default: break;
        }
    }
    u16 f = chunk_open(a0);
    if (!f) return 0;
    u16 ncols = MTX(G_mtx_ncols, 0x6456), nrows = MTX(G_mtx_nrows, 0x6458), rows = MTX(G_mtx_rows, 0x641A);
    u16 mk = stk_mark();
    bool ok = found(chunk_find(f, ds_lit("BMP:MTX:"), 0));
    stk_release(mk);
    if (ok) {
        my_fread(ncols, 2, 1, f);
        my_fread(nrows, 2, 1, f);
        for (u16 r = 0; r < DSW(nrows) && ok; r++) {
            if (DSW((u16)(rows + 2 * r))) mtx_free_row((s16)r);
            DSW((u16)(rows + 2 * r)) = L_malloc((u16)(DSW(ncols) * 2));
            if (!DSW((u16)(rows + 2 * r))) ok = false;
        }
    }
    if (!ok) {
        mtx_free_rows();
        chunk_close(f);
        return 0;
    }
    for (u16 c = 0; c < DSW(ncols); c++)
        for (u16 r = 0; r < DSW(nrows); r++)
            my_fread((u16)(DSW((u16)(rows + 2 * r)) + 2 * c), 2, 1, f);
    chunk_close(f);
    return 1;
}
