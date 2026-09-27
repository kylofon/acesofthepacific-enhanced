/* The TTM animation-script player (video_ttm.md §4-§8, §10, §11): loader / indexer / run / free
 * (INTRO 1125 / PS 3b02 / AP 24fc), the interpreter and draw dispatch (INTRO 11d2 / PS 3baf / AP 2595,
 * the AP copy reduced: no page reset, no update rectangles, no sound, fades, wipes, MTX or delay) and
 * the wipes. Transcribed from the INTRO disassembly; PS is the same code at +1 (callee differences
 * marked), AP from 2595/24fc. */
#include "video/vid_priv.h"

#include "platform/platform.h"
#include "platform/rtl.h"
#include "shell_data/shell_data.h"
#include "sound/sound.h"

static bool is_ap(void) { return pid() == PROG_AP; }

/* ---------------------------------------------------------------- record / context access */

static FarPtr rec(void) { return ds_far(lds(G_ttm_rec)); }
static void rec_set(FarPtr r) { ds_far_wr(lds(G_ttm_rec), r); }
static FarPtr head(void) { return ds_far(lds(G_ttm_rec_list)); }
static bool fnull(FarPtr p) { return (p.off | p.seg) == 0; }
static u16 RW(u16 o) { FarPtr r = rec(); return rd16(r.seg, (u16)(r.off + o)); }
static void RWset(u16 o, u16 v) { FarPtr r = rec(); wr16(r.seg, (u16)(r.off + o), v); }
static FarPtr RF(u16 o) { FarPtr r = rec(); return far_rd(r.seg, (u16)(r.off + o)); }
static void RFset(u16 o, FarPtr v) { FarPtr r = rec(); far_wr(r.seg, (u16)(r.off + o), v); }
enum { R_ID = 0, R_COUNT = 2, R_CODE = 4, R_END = 8, R_BMP = 0x0C, R_FONT = 0x18, R_PAL = 0x24, R_SAVED = 0x3C,
       R_SX = 0x6C, R_SY = 0x84, R_SW = 0x9C, R_SH = 0xB4, R_NEXT = 0xCC, R_FRAMES = 0xD0 };
static FarPtr frame_ptr(s16 i) { FarPtr t = RF(R_FRAMES); return far_rd(t.seg, (u16)(t.off + 4 * i)); }

static u16 ctx(void) { return LW(G_ttm_ctx); }
#define CW(o) DSW((u16)(ctx() + (o)))
#define CB(o) DSB((u16)(ctx() + (o)))

static u16 str_slot(int i) { return DSW((u16)(lds(G_ttm_str_slot) + 2 * i)); }
static FarPtr pending(void) { return ds_far(lds(G_ttm_pending_pal)); }
static void pending_set(FarPtr p) { ds_far_wr(lds(G_ttm_pending_pal), p); }
static FarPtr far_of(u32 v) { return far_make((u16)(v >> 16), (u16)v); }
static s16 scr_w(void) { return DRVS(D_SCR_W); }
static s16 scr_h(void) { return DRVS(D_SCR_H); }

/* program-specific DS strings of the loader: "PAG:", "TT3:", the unpack mode */
static u16 str_pag(void) { return BY_PROG(0x1E70, 0x3858, 0x2212); }
static u16 str_tt3(void) { return BY_PROG(0x1E75, 0x385D, 0x2217); }
static u16 str_pkmode(void) { return BY_PROG(0x1E7A, 0x3862, 0x221C); }

/* ---------------------------------------------------------------- §4 loader / indexer */

/* INTRO 1125:0006 / PS 3b02:0007 / AP 24fc:0009 */
u16 ttm_peek(FarPtr p) { return rd16(p.seg, p.off); }

/* INTRO 1125:0013 / PS 3b02:0014 / AP 24fc:0016 */
void ttm_init(void)
{
    if (DRVB(D_ADAPTER) == 0x0C) LW(G_ttm_level) = 2;
    if (!is_ap()) upd_alloc_nodes(100);
}

/* INTRO 1125:002f / PS 3b02:0030 / AP 24fc:0028 */
s16 ttm_load(u16 arg)
{
    u16 h;
    bool close;
    FarPtr buf = far_make(0, 0);
    if (chunk_is_open(arg) == 0) { h = chunk_open(arg); close = true; }
    else { h = arg; close = false; }
    if (!h) return -1;
    u16 m = stk_mark(), cnt = stk_alloc(2);
    s16 ret = -1;
    if (chunk_find(h, str_pag(), 0) == -1) DSW(cnt) = 0x14A;
    else my_fread(cnt, 2, 1, h);
    if (chunk_find(h, str_tt3(), 0) == -1) { if (close) chunk_close(h); goto out; }
    s32 size = chunk_size(h);
    s16 pk = pk_open(-1, h, str_pkmode(), (u32)size);
    if (pk < 0) {
        if (close) { chunk_close(h); goto out; }
        ret = pk;
        goto out;
    }
    u16 usize = (u16)pk_size(pk);
    buf = dos_alloc((u32)(s32)(s16)usize, 0, 0);
    if (fnull(buf)) {
        u16 r = pk_close(pk);
        if (close) { chunk_close(h); goto out; }
        ret = (s16)r;
        goto out;
    }
    u16 got = pk_read(pk, buf, usize);
    pk_close(pk);
    if (close) chunk_close(h);
    if (got != usize) { dos_free(buf); goto out; }
    /* append a record at the tail of the list */
    FarPtr r;
    if (fnull(head())) {
        r = dos_alloc(0xD4, 0, 1);
        ds_far_wr(lds(G_ttm_rec_list), r);
    } else {
        FarPtr t = head();
        while (!fnull(far_rd(t.seg, (u16)(t.off + R_NEXT)))) t = far_rd(t.seg, (u16)(t.off + R_NEXT));
        r = dos_alloc(0xD4, 0, 1);
        far_wr(t.seg, (u16)(t.off + R_NEXT), r);
    }
    rec_set(r);
    if (fnull(r)) goto out;
    RWset(R_COUNT, DSW(cnt));
    FarPtr ft = dos_alloc((u32)(u16)((DSW(cnt) + 1) << 2), 0, 1);
    RFset(R_FRAMES, ft);
    if (fnull(ft)) goto out;
    RFset(R_CODE, buf);
    RFset(R_END, far_make(buf.seg, (u16)(buf.off + usize - 1)));
    RWset(R_ID, 0);
    ret = 1;
out:
    stk_release(m);
    return ret;
}

/* INTRO 1125:02b7 / PS 3b02:02b8 / AP 24fc:02c9 */
s16 ttm_find(s16 id)
{
    for (FarPtr p = head(); ; p = far_rd(p.seg, (u16)(p.off + R_NEXT))) {
        rec_set(p);
        if (fnull(p)) return 0;
        if ((s16)rd16(p.seg, p.off) == id) return 1;
    }
}

/* INTRO 1125:02f8 / PS 3b02:02f9 / AP 24fc:0311 */
void ttm_new_ctx(s16 frame, u16 id, u16 tag)
{
    u16 c;
    if (LW(G_ttm_ctx_list) == 0) {
        c = rtl_malloc(0x3D);
        LW(G_ttm_ctx_list) = c;
    } else {
        u16 s = LW(G_ttm_ctx_list);
        while (DSW(s + 0x3B)) s = DSW(s + 0x3B);
        c = rtl_malloc(0x3D);
        DSW(s + 0x3B) = c;
    }
    LW(G_ttm_ctx) = c;
    if (!c) return;
    rtl_memset(c, 0, 0x3D);
    DSW(c + 4) = (u16)frame;
    DSW(c + 8) = (u16)frame;
    DSW(c) = id;
    DSW(c + 2) = tag;
    DSW(c + 0x0A) = 0xFFFF;
}

/* INTRO 1125:0380 / PS 3b02:0381 / AP 24fc:039c */
FarPtr ttm_skip_str(FarPtr p)
{
    u16 n = 0;
    while (rd8(p.seg, p.off++)) n++;
    if (!(n & 1)) p.off++;
    return p;
}

/* INTRO 1125:03a7 / PS 3b02:03a8 / AP 24fc:03c5 */
s16 ttm_index(s16 id)
{
    s16 tagframe = 0;
    if (!ttm_find(id)) return 0;
    if (fnull(rec())) return 0;
    if (fnull(RF(R_CODE))) return 0;
    FarPtr p = RF(R_CODE), q = p;
    for (s16 i = 0; (s16)RW(R_COUNT) >= i; i++) {
        FarPtr t = RF(R_FRAMES);
        far_wr(t.seg, (u16)(t.off + 4 * i), far_make(0, 0));
    }
    for (s16 i = 0; (s16)RW(R_COUNT) > i; i++) {
        FarPtr t = RF(R_FRAMES);
        far_wr(t.seg, (u16)(t.off + 4 * i), p);
        for (;;) {
            if (!(RW(R_END) > p.off)) return 0;
            p.off += 2;
            u16 op = ttm_peek(q);
            if (op == 0x0FF0) { tagframe++; q = p; break; }
            u16 n = op & 0x0F;
            switch (n) {
            case 0: break;
            case 1: {
                u16 a = ttm_peek(p);
                p.off += 2;
                if (op == 0x1111) ttm_new_ctx(tagframe, (u16)id, a);
                break;
            }
            case 2: case 3: case 4: case 5: case 6: case 7:
                for (u16 k = n; k-- > 0;) ttm_peek(far_add(p, (u16)(2 * k)));   /* read and discarded */
                p.off = (u16)(p.off + 2 * n);
                break;
            case 15:
                if (op == 0xAF1F || op == 0xAF2F) {
                    u16 c = (u16)(ttm_peek(p) << 1);
                    do { p.off += 2; } while (c-- != 0);
                } else {
                    p = ttm_skip_str(p);
                }
                break;
            default:
                return -1;
            }
            q = p;
        }
    }
    return 1;
}

/* INTRO 1125:065c / PS 3b02:065d / AP 24fc:067d */
void ttm_apply_pal(void)
{
    if (!fnull(pending())) {
        pal_set(pending());
        pending_set(far_make(0, 0));
    }
}

/* INTRO 1125:060e / PS 3b02:060f / AP 24fc:062f */
s16 ttm_run_frame_checked(s16 frame)
{
    if (fnull(rec())) return 0;
    if ((s16)RW(R_COUNT) <= frame || frame < 0 || fnull(frame_ptr(frame))) return 0;
    return ttm_run_frame(frame);
}

/* INTRO 1125:05ee / PS 3b02:05ef / AP 24fc:0610 */
s16 ttm_run(u16 frame)
{
    s16 r = ttm_run_frame_checked((s16)frame);
    ttm_apply_pal();
    return r;
}

/* INTRO 1125:06c5 / PS 3b02:06c6 / AP 24fc:06ec */
void ttm_free(s16 id)
{
    if (!ttm_find(id)) return;
    if (LB(G_ttm_mtx_open)) { mtx_close(); LB(G_ttm_mtx_open) = 0; }
    for (int i = 0; i < 6; i++)
        if (RW((u16)(R_FONT + 2 * i))) { font_free(RW((u16)(R_FONT + 2 * i))); RWset((u16)(R_FONT + 2 * i), 0); }
    for (int i = 0; i < 6; i++)
        if (RW((u16)(R_BMP + 2 * i))) { bmp_free_w(RW((u16)(R_BMP + 2 * i))); RWset((u16)(R_BMP + 2 * i), 0); }
    for (int i = 0; i < 6; i++) {
        FarPtr p = RF((u16)(R_PAL + 4 * i));
        if (fnull(p)) continue;
        if (p.seg == pending().seg && p.off == pending().off) pending_set(far_make(0, 0));
        pal_free(RF((u16)(R_PAL + 4 * i)));
        RFset((u16)(R_PAL + 4 * i), far_make(0, 0));
    }
    for (int i = 0; i < 12; i++) {
        FarPtr p = RF((u16)(R_SAVED + 4 * i));
        if (fnull(p)) continue;
        dos_free(p);
        RFset((u16)(R_SAVED + 4 * i), far_make(0, 0));
    }
    if (!fnull(RF(R_FRAMES))) dos_free(RF(R_FRAMES));
    RFset(R_FRAMES, far_make(0, 0));
    if (!fnull(RF(R_CODE))) dos_free(RF(R_CODE));
    RFset(R_CODE, far_make(0, 0));
    FarPtr r = rec(), h = head();
    if (r.seg == h.seg && r.off == h.off) {
        ds_far_wr(lds(G_ttm_rec_list), RF(R_NEXT));
    } else {
        FarPtr prev = far_make(0, 0), p = h;
        while (!fnull(p)) {
            if (p.seg == r.seg && p.off == r.off) break;
            prev = p;
            p = far_rd(p.seg, (u16)(p.off + R_NEXT));
        }
        far_wr(prev.seg, (u16)(prev.off + R_NEXT), far_rd(p.seg, (u16)(p.off + R_NEXT)));
    }
    RFset(R_NEXT, far_make(0, 0));
    dos_free(rec());
}

/* INTRO 1125:0685 / PS 3b02:0686 / AP 24fc:06a6 */
void ttm_free_all(void)
{
    while (!fnull(head())) { FarPtr h = head(); ttm_free((s16)rd16(h.seg, h.off)); }
    while (LW(G_ttm_ctx) != 0) {
        LW(G_ttm_ctx_list) = DSW(LW(G_ttm_ctx) + 0x3B);
        rtl_free(LW(G_ttm_ctx));
        LW(G_ttm_ctx) = LW(G_ttm_ctx_list);
    }
}

/* INTRO 1125:095c / PS 3b02:095d / AP 24fc:0989 (AP: free_all only) */
void ttm_shutdown(void)
{
    if (!is_ap()) upd_free_all();
    ttm_free_all();
}

/* ---------------------------------------------------------------- §8, §10 (INTRO/PS) */

/* INTRO 1125:096a / PS 3b02:096b */
void ttm_update_rect(u16 x, u16 y, u16 w, u16 h)
{
    FarPtr z = far_make(0, 0);
    if (LW(G_ttm_dblbuf)) {
        if (!LW(G_ttm_no_restore) && LS(G_ttm_level) >= 3)
            upd_add(x, y, w, h, 1, DRVW(D_PAGE_BG), DRVW(D_PAGE_BACK), 1, z);
    } else {
        upd_add(x, y, w, h, 1, DRVW(D_PAGE_BACK), DRVW(D_PAGE_FRONT), 0, z);
        upd_add(x, y, w, h, 1, DRVW(D_PAGE_BACK), DRVW(D_PAGE_FRONT), 1, z);
        if (!LW(G_ttm_no_restore) && LS(G_ttm_level) >= 3)
            upd_add(x, y, w, h, 1, DRVW(D_PAGE_BG), DRVW(D_PAGE_BACK), 0, z);
    }
}

/* INTRO 1125:0a93 / PS 3b02:0a94 */
void ttm_copy_back_to_bg(s16 x, s16 y, s16 w, s16 h)
{
    if (LS(G_ttm_level) < 3) return;
    u16 r = DRVW(D_PAGE_READ), wr = DRVW(D_PAGE_WRITE);
    DRVW(D_PAGE_READ) = DRVW(D_PAGE_BACK);
    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_BG);
    gfx_copy_rect(x, y, w, h);
    DRVW(D_PAGE_READ) = r;
    DRVW(D_PAGE_WRITE) = wr;
}

/* INTRO 1125:0a2c / PS 3b02:0a2d */
void ttm_save_bg(void)
{
    LW(G_ttm_full_dirty) = 1;
    if (LS(G_ttm_level) >= 3) {
        ttm_copy_back_to_bg(0, 0, scr_w(), scr_h());
        upd_discard(DRVW(D_PAGE_BG), DRVW(D_PAGE_BACK), LW(G_ttm_dblbuf));
    } else {
        upd_add(0, 0, (u16)scr_w(), (u16)scr_h(), 1, DRVW(D_PAGE_BACK), DRVW(D_PAGE_FRONT), 0, far_make(0, 0));
    }
}

/* ---------------------------------------------------------------- §5 helpers */

/* INTRO 11d2:1546 / PS 3baf:1547 / AP 2595:0ad9 */
s16 ttm_find_tag(u16 tag)
{
    for (s16 i = 0; ; i++) {
        FarPtr f = frame_ptr(i);
        if (fnull(f)) return -1;
        u16 op = rd16(f.seg, f.off);
        if ((op == 0x1101 || op == 0x1111) && rd16(f.seg, (u16)(f.off + 2)) == tag) return i;
    }
}

/* INTRO 11d2:15b3 / PS 3baf:15b4 / AP 2595:0b4a — PORT: the 100-byte buffer overflows into the
 * following globals exactly as the original (no bound). */
u16 ttm_copy_arg(s16 count, FarPtr src)
{
    u16 b = lds(G_ttm_argbuf), di = 0;
    if (!(count >= scr_h())) {
        if (count == 0) {
            while (rd8(src.seg, src.off)) { DSB((u16)(b + di)) = rd8(src.seg, src.off); src.off++; di++; }
        } else {
            while (count-- != 0) { DSB((u16)(b + di)) = rd8(src.seg, src.off); src.off++; di++; }
        }
    }
    DSB((u16)(b + di)) = 0;
    return b;
}

/* INTRO 11d2:1607 / PS 3baf:1608 / AP 2595:0b9e: &rec->font[ctx->font_slot] */
FarPtr ttm_cur_font_ptr(void)
{
    FarPtr r = rec();
    return far_make(r.seg, (u16)(r.off + 2 * CW(0x10) + R_FONT));
}

/* INTRO 11d2:1630 / PS 3baf:1631 */
void ttm_cycle_add(s16 i, s16 dir)
{
    u16 f = lds(G_ttm_cyc_first), l = lds(G_ttm_cyc_last);
    pal_cycle_block(DSS(f + 2 * i), (s16)(DSS(l + 2 * i) - DSS(f + 2 * i) + 1), dir);
}

/* INTRO 11d2:165f / PS 3baf:1660 */
u16 ttm_page_by_index(s16 v)
{
    if (v == 0) return DRVW(D_PAGE_FRONT);
    if (v == 2) return DRVW(D_PAGE_BG);
    return DRVW(D_PAGE_BACK);
}

/* INTRO 11d2:0148 / PS 3baf:0149 / AP 2595:0002 */
void ttm_sprite_col(u16 fr, s16 x, s16 y, s16 flip, u8 col)
{
    if (DSS(fr + 4) != -2) return;
    DSW(fr) = (u16)(DSW(fr) + (DSW(fr + 2) >> 4));
    DSW(fr + 2) &= 0x0F;
    u16 s = DSW(fr), o = DSW(fr + 2);
    u8 save = rd8(s, o);
    wr8(s, o, col);
    gfx_sprite_scn_ind(fr, x, y, flip);
    wr8(s, o, save);
}

/* ---------------------------------------------------------------- §11 wipes (INTRO/PS) */

/* PORT: the wipes copy straight into the displayed page with CPU-bound loops; present after each step
 * and pace (about 1 ms per step / per dly()). */
static void pace(void) { vid_present(); host_wait_until_ns(host_ns() + 1000000u); }

/* INTRO 11d2:19ea / PS 3baf:19eb */
void ttm_wipe_copy(s16 x, s16 y, s16 w, s16 h)
{
    if (w >= 3 && h >= 3) gfx_copy_rect(x, y, w, h);
}

/* INTRO 11d2:19c4 / PS 3baf:19c5 — 100 empty copies */
void ttm_wipe_delay(void)
{
    for (s16 i = 1; i <= 100; i++) ttm_wipe_copy(0, 0, 0, 0);
    pace();
}

/* INTRO 11d2:1688 / PS 3baf:1689 */
void ttm_wipe(s16 x, s16 y, s16 w, s16 h, s16 a4, u16 op)
{
    u16 saved = DRVW(D_PAGE_WRITE);
    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_FRONT);
    s16 x1 = x, x2 = (s16)(x + w), y1 = y, y2 = (s16)(y + h);
    switch (op) {
    case 0xA014:
        ttm_dissolve((u16)x, (u16)y, (u16)w, (u16)h);
        break;
    case 0xA024:
        ttm_wipe_copy(x, y, w, h);
        break;
    case 0xA034: case 0xA094: {                                   /* box closing / opening */
        s16 hw = (s16)(w >> 1), hh = (s16)(h >> 1);
        s16 cx = (s16)(x + hw), cy = (s16)(y + hh);
        s16 m = hw > hh ? hw : hh;
        /* PORT: LDIV by m = 0 (w, h < 2) is not reached by the data; guarded as 0 */
        s32 fx = m ? (s32)hw * 1000 / m : 0, fy = m ? (s32)hh * 1000 / m : 0;
        s16 s = op == 0xA034 ? m : 1;
        for (;;) {
            if (op == 0xA034) { if (!(s >= 1)) break; s--; }
            else { if (!(s <= m)) break; s++; }
            s16 dx = (s16)(fx * s / 1000), dy = (s16)(fy * s / 1000);
            s16 bx = (s16)(cx - dx), by = (s16)(cy - dy), bw = (s16)(dx << 1), bh = (s16)(dy << 1);
            if (op == 0xA034) {
                ttm_wipe_copy(x1, y1, (s16)(bx - x1), (s16)(y2 - y1));
                ttm_wipe_copy((s16)(bx + bw), y1, (s16)(x2 - bx - bw), (s16)(y2 - y1));
                ttm_wipe_copy(bx, y1, bw, (s16)(by - y1));
                ttm_wipe_copy(bx, (s16)(by + bh), bw, (s16)(y2 - by - bh));
            } else {
                ttm_wipe_copy(bx, by, bw, bh);
            }
            pace();
        }
        break;
    }
    case 0xA044:                                                  /* blinds */
        for (s16 s = 1; s <= 10; s++)
            for (s16 o = 0; (s16)(y1 + s + o) <= y2; o = (s16)(o + 10)) {
                if (w) gfx_copy_rect(x1, (s16)(y1 + s + o), w, 1);
                ttm_wipe_delay();
            }
        break;
    case 0xA054: case 0xA064: {                                   /* vertical strips */
        s16 lim = w;
        for (s16 s = 3; (s16)(lim - 3) >= s; s++) {
            if (op == 0xA064) x = (s16)(x2 - s);
            else x = (s16)(s - 3);                                /* absolute: ignores x (quirk) */
            ttm_wipe_copy(x, y, 3, h);
            pace();
        }
        break;
    }
    case 0xA0B5:
        ttm_dissolve_timed((u16)x, (u16)y, (u16)w, (u16)h, (u16)a4);
        break;
    default: {                                                    /* A074 / A084 / others: bands */
        s16 lim = h;
        for (s16 s = 3; (s16)(lim - 3) >= s; s++) {
            if (op == 0xA084) y = (s16)(y2 - s);
            else if (op == 0xA074) y = (s16)(s - 3);              /* absolute (quirk) */
            ttm_wipe_copy(x, y, w, 3);
            ttm_wipe_delay();
        }
        break;
    }
    }
    DRVW(D_PAGE_WRITE) = saved;
    vid_present();
}

/* ---------------------------------------------------------------- §7 draw dispatch */

/* INTRO 11d2:01a6 / PS 3baf:01a7 / AP 2595:0061 */
s16 ttm_draw_op(s16 x, s16 y, s16 a2, s16 a3, s16 a4, s16 a5, s16 a6, u16 str)
{
    u16 op = LW(G_ttm_op);
    switch (op) {
    case 0xA0A4: gfx_line(x, y, a2, a3); return 0;
    case 0xA114: DRVB(D_FILL_ON) = 0; gfx_rect(x, y, a2, a3); DRVB(D_FILL_ON) = 1; return 0;
    case 0xA104: DRVB(D_FILL_ON) = 1; gfx_rect(x, y, a2, a3); return 0;
    case 0xA424: case 0xA404: {
        DRVB(D_FILL_ON) = op == 0xA404;
        s16 r = (s16)(a3 >> 1);
        gfx_circle(r, (s16)(x + r), (s16)(y + r));
        DRVB(D_FILL_ON) = 1;
        return 0;
    }
    case 0xAF1F: case 0xAF2F: {
        DRVB(D_FILL_ON) = op == 0xAF2F;
        s16 k = DSS(str);
        gfx_polygon(k, (u16)(str + 2), (u16)(str + 2 * k + 2));
        DRVB(D_FILL_ON) = 1;
        return 0;
    }
    case 0xA002: gfx_put_pixel(x, y, (s8)DRVB(D_DRAW_COL)); return 0;
    case 0xA704:
        if (is_ap()) break;
        if (LB(G_ttm_mtx_open)) mtx_draw((u16)x, (u16)y, (u16)a2, (u16)a3);
        return 0;
    case 0xA504: case 0xA505: case 0xA506: case 0xA514: case 0xA515: case 0xA516:
    case 0xA524: case 0xA525: case 0xA526: case 0xA534: case 0xA535: case 0xA536: case 0xA5A7: {
        LW(G_ttm_flip) = (u16)((op & 0xF0) >> 4);
        u16 h = RW((u16)(R_BMP + 2 * a3));
        if (!h) return 0;
        u16 fr = DSW((u16)(h + 2 * a2));
        if (!fr) return 0;
        switch (op & 0x0F) {
        case 7: gfx_sprite_rotated_ind(fr, x, y, (u16)a6, a4, a5, (s16)(DSS(fr + 6) >> 1), (s16)(DSS(fr + 8) >> 1)); break;
        case 6: gfx_sprite_scaled(fr, x, y, LS(G_ttm_flip), a4, a5); break;
        case 5: ttm_sprite_col(fr, x, y, LS(G_ttm_flip), (u8)a4); break;
        default: bmp_draw(fr, x, y, LS(G_ttm_flip)); break;
        }
        return 0;
    }
    case 0xA601: {
        if (LW(G_ttm_skip)) return 0;
        FarPtr s = RF((u16)(R_SAVED + 4 * x));
        if (fnull(s)) return 0;
        gfx_put_rect(s, (s16)RW((u16)(R_SX + 2 * x)), (s16)RW((u16)(R_SY + 2 * x)), (s16)RW((u16)(R_SW + 2 * x)),
                     (s16)RW((u16)(R_SH + 2 * x)));
        return 0;
    }
    case 0xAF02: gfx_flood(x, y, (s16)DRVB(D_FILL_COL)); return 0;
    case 0xA304:
        if (is_ap()) break;
        ttm_textbox((u16)x, (u16)y, (u16)a2, (u16)a3);
        return 0;
    default: break;
    }
    if ((op >= 0xA014 && op <= 0xA094) || op == 0xA0B5) {
        if (is_ap()) return 0;                                    /* AP: wipes are no-ops */
        if (!LW(G_ttm_skip) && a2 > 0 && a3 > 0 && (s16)(x + a2) <= scr_w() && (s16)(y + a3) <= scr_h())
            ttm_wipe(x, y, a2, a3, a4, op);
        return 0;
    }
    if (op >= 0xA204 && op <= 0xA294) {
        gfx_font_select((s16)RW((u16)(R_FONT + 2 * CW(0x10))));
        DRVB(D_TEXT_FLAGS) = 1;
        gfx_text(str_slot((op - 0xA204) >> 4), x, y);
        DRVB(D_TEXT_FLAGS) = 0;
        return 0;
    }
    return -1;
}

/* ---------------------------------------------------------------- §5 the interpreter */

static void fade_lvl(s16 a0, s16 a1, s16 a2, s16 v)
{
    s16 l = (s16)(v / 10);
    if (l < 0) l = 0;
    else if (l > 0x3F) l = 0x3F;
    pal_fade(a0, a1, a2, l);
}

/* the update rectangle after an A-opcode (§8, table CS:1422) */
static void after_draw(u16 op, s16 a[7])
{
    s16 x = a[0], y = a[1];
    switch (op) {
    case 0xA5A7: {
        u16 m = stk_mark(), b = stk_alloc(8);
        DSS(b) = a[0]; DSS(b + 2) = a[1]; DSS(b + 4) = a[4]; DSS(b + 6) = a[5];
        gfx_last_bbox(b, (u16)(b + 2), (u16)(b + 4), (u16)(b + 6));
        a[0] = DSS(b); a[1] = DSS(b + 2); a[4] = DSS(b + 4); a[5] = DSS(b + 6);
        stk_release(m);
        a[4] = (s16)(a[4] - a[0]);
        a[5] = (s16)(a[5] - a[1]);
        ttm_update_rect((u16)a[0], (u16)a[1], (u16)a[4], (u16)a[5]);
        return;
    }
    case 0xA704:
        if (LB(G_ttm_mtx_open)) ttm_update_rect((u16)x, (u16)y, (u16)a[2], (u16)a[3]);
        return;
    case 0xA504: case 0xA505: case 0xA506: case 0xA514: case 0xA515: case 0xA516:
    case 0xA524: case 0xA525: case 0xA526: case 0xA534: case 0xA535: case 0xA536: {
        u16 h = RW((u16)(R_BMP + 2 * a[3]));
        if (!h) return;
        u16 fr = DSW((u16)(h + 2 * a[2]));
        if (!fr) return;
        if ((op & 0x0F) == 6) { ttm_update_rect((u16)x, (u16)y, (u16)a[4], (u16)a[5]); return; }
        s16 w = DSS(fr + 6);
        if (w < 0) w = (s16)-w;
        if (!w) return;
        ttm_update_rect((u16)x, (u16)y, (u16)w, (u16)(DSS(fr + 8) + 2));
        return;
    }
    case 0xA404: case 0xA424:
        ttm_update_rect((u16)(x - 20), (u16)y, (u16)(a[2] + 0x30), (u16)(a[3] + 1));
        return;
    case 0xAF1F: case 0xAF2F:
        ttm_update_rect(0, 0, (u16)scr_w(), (u16)scr_h());
        return;
    case 0xA601:
        if (!fnull(RF((u16)(R_SAVED + 4 * x))))
            ttm_update_rect(RW((u16)(R_SX + 2 * x)), RW((u16)(R_SY + 2 * x)), RW((u16)(R_SW + 2 * x)), RW((u16)(R_SH + 2 * x)));
        return;
    case 0xA002:
        ttm_update_rect((u16)x, (u16)y, 8, 1);
        return;
    case 0xA0A4: {
        s16 mx = x < a[2] ? x : a[2], my = y < a[3] ? y : a[3];
        s16 dx = (s16)(x - a[2]), dy = (s16)(y - a[3]);
        if (dx < 0) dx = (s16)-dx;
        if (dy < 0) dy = (s16)-dy;
        ttm_update_rect((u16)mx, (u16)my, (u16)(dx + 8), (u16)(dy + 2));
        return;
    }
    case 0xAF02:
        return;
    default:
        ttm_update_rect((u16)x, (u16)y, (u16)(a[2] + 8), (u16)(a[3] + 2));
        return;
    }
}

/* INTRO 11d2:053f / PS 3baf:0540 / AP 2595:038b */
s16 ttm_run_frame(s16 frame)
{
    if (fnull(rec())) return 0;
    const bool ap = is_ap();
    FarPtr p = frame_ptr(frame);
    LW(G_ttm_skip) = CW(0x0E);
    LW(G_ttm_goto) = 0xFFFF;
    for (;;) {
        LW(G_ttm_op) = rd16(p.seg, p.off);
        u16 n = LW(G_ttm_op) & 0x0F;
        if (LW(G_ttm_op) == 0x1301) LW(G_ttm_op) = 0xC051;
        if (LW(G_ttm_op) == 0x1311) LW(G_ttm_op) = 0xC061;
        if (LW(G_ttm_op) == 0x0FF0) break;
        if (!ap) { DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_BACK); DRVW(D_PAGE_READ) = DRVW(D_PAGE_BACK); }
        p.off += 2;
        s16 a[7];
        for (int i = 0; i < 7; i++) a[i] = (s16)rd16(p.seg, (u16)(p.off + 2 * i));
        u16 str;
        if (n == 0x0F) {
            if (LW(G_ttm_op) == 0xAF1F || LW(G_ttm_op) == 0xAF2F) {
                str = ttm_copy_arg((s16)(a[0] << 2), far_add(p, 2));
                p.off = (u16)(p.off + (a[0] << 2) + 2);
            } else {
                str = ttm_copy_arg(0, p);
                p = ttm_skip_str(p);
            }
        } else {
            str = 0;
            p.off = (u16)(p.off + 2 * n);
        }
        u16 op = LW(G_ttm_op);
        if (LW(G_ttm_gosub_depth) && op >= 0xA000 && op <= 0xAFFF) {
            s16 dx = LS(G_ttm_gosub_dx), dy = LS(G_ttm_gosub_dy);
            if (op == 0xA0A4) { a[2] = (s16)(a[2] + dx); a[3] = (s16)(a[3] + dy); }
            if (!(op == 0xA601 || op == 0xAF1F || op == 0xAF2F)) { a[0] = (s16)(a[0] + dx); a[1] = (s16)(a[1] + dy); }
        }
        if ((op & 0xF000) == 0xC000) {
            if (!ap && !LW(G_ttm_skip)) ttm_sound_op((u16)a[0], (u16)a[1], (u16)a[2], (u16)a[3], str);
            goto next;
        }
        if ((op & 0xF000) == 0xA000) {
            ttm_draw_op(a[0], a[1], a[2], a[3], a[4], a[5], a[6], str);
            goto next;
        }
        {
            const bool skip = LW(G_ttm_skip) != 0;
            u16 s;
            switch (op) {
            case 0x2002:                                              /* SET_COLORS */
                CB(0x1E) = (u8)a[0]; DRVB(D_DRAW_COL) = (u8)a[0]; DRVB(D_TEXT_FG) = (u8)a[0];
                CB(0x1F) = (u8)a[1]; DRVB(D_FILL_COL) = (u8)a[1];
                break;
            case 0xF01F:                                              /* LOAD_SCR */
                if (skip) break;
                /* PORT: platform.h names scr_load's first word "page"; it is the name (0f35:000a [bp+6]) */
                scr_load(str, 0);
                if (!ap) ttm_save_bg();
                break;
            case 0x1031: CW(0x1C) = (u16)a[0]; break;                /* SET_BRUSH */
            case 0x1051: CW(0x14) = (u16)a[0]; break;                /* SEL_BMP */
            case 0xF02F:                                              /* LOAD_BMP */
                if (skip) break;
                s = CW(0x14);
                if (RW((u16)(R_BMP + 2 * s))) bmp_free_w(RW((u16)(R_BMP + 2 * s)));
                RWset((u16)(R_BMP + 2 * s), bmp_load(str));
                break;
            case 0x0080:                                              /* FREE_BMP */
                if (skip) break;
                s = CW(0x14);
                if (RW((u16)(R_BMP + 2 * s))) bmp_free_w(RW((u16)(R_BMP + 2 * s)));
                RWset((u16)(R_BMP + 2 * s), 0);
                break;
            case 0x1121: CW(0x16) = (u16)a[0]; break;                /* SEL_GETPUT */
            case 0x4214: {                                            /* SAVE_GETPUT */
                if (skip) break;
                s = CW(0x16);
                RWset((u16)(R_SX + 2 * s), (u16)a[0]);
                RWset((u16)(R_SY + 2 * s), (u16)a[1]);
                RWset((u16)(R_SW + 2 * s), (u16)a[2]);
                RWset((u16)(R_SH + 2 * s), (u16)a[3]);
                u16 so = (u16)(R_SAVED + 4 * s);
                if (!fnull(RF(so))) { dos_free(RF(so)); RFset(so, far_make(0, 0)); }
                u16 size = (u16)gfx_rect_size((u16)a[2], (u16)a[3]);
                if (!size) break;
                RFset(so, dos_alloc(size, 0, 0));
                if (fnull(RF(so))) break;
                gfx_get_rect(RF(so), a[0], a[1], a[2], a[3]);
                break;
            }
            case 0x00C0: {                                            /* FREE_GETPUT */
                if (skip) break;
                u16 so = (u16)(R_SAVED + 4 * CW(0x16));
                if (!fnull(RF(so))) dos_free(RF(so));
                RFset(so, far_make(0, 0));
                break;
            }
            case 0x1071:                                              /* SEL_FONT */
                CW(0x10) = (u16)a[0];
                gfx_font_select((s16)RW((u16)(R_FONT + 2 * a[0])));
                break;
            case 0xF04F: {                                            /* LOAD_FONT */
                if (skip) break;
                FarPtr f = ttm_cur_font_ptr();
                if (rd16(f.seg, f.off)) font_free(rd16(f.seg, f.off));
                u16 v = font_load(str);
                wr16(f.seg, f.off, v);
                gfx_font_select((s16)v);
                break;
            }
            case 0x0090: {                                            /* FREE_FONT */
                if (skip) break;
                FarPtr f = ttm_cur_font_ptr();
                if (rd16(f.seg, f.off)) font_free(rd16(f.seg, f.off));
                wr16(f.seg, f.off, 0);
                break;
            }
            case 0x1061:                                              /* SEL_PAL */
                CW(0x12) = (u16)a[0];
                if (skip) break;
                pending_set(RF((u16)(R_PAL + 4 * a[0])));
                if (DRVB(D_ADAPTER) == 4) pal_set(pending());
                break;
            case 0xF05F: {                                            /* LOAD_PAL */
                if (skip) break;
                s = CW(0x12);
                if (!fnull(RF((u16)(R_PAL + 4 * s)))) pal_free(RF((u16)(R_PAL + 4 * s)));
                FarPtr pl = far_of(pal_load(str));
                RFset((u16)(R_PAL + 4 * s), pl);
                pending_set(pl);
                break;
            }
            case 0x0070: {                                            /* FREE_PAL */
                if (skip) break;
                s = CW(0x12);
                FarPtr pl = RF((u16)(R_PAL + 4 * s));
                if (!fnull(pl)) {
                    if (pl.seg == pending().seg && pl.off == pending().off) pending_set(far_make(0, 0));
                    pal_free(RF((u16)(R_PAL + 4 * s)));
                }
                RFset((u16)(R_PAL + 4 * s), far_make(0, 0));
                break;
            }
            case 0x4004:                                              /* SET_CLIP */
                CW(0x21) = (u16)a[0]; DRVW(D_CLIP_X1) = (u16)a[0];
                CW(0x25) = (u16)a[1]; DRVW(D_CLIP_Y1) = (u16)a[1];
                CW(0x23) = (u16)a[2]; DRVW(D_CLIP_X2) = (u16)a[2];
                CW(0x27) = (u16)a[3]; DRVW(D_CLIP_Y2) = (u16)a[3];
                CB(0x20) = 1; DRVB(D_CLIP_ON) = 1;
                break;
            case 0x0110: LW(G_ttm_purge) = 1; break;                 /* PURGE */
            case 0x1101: case 0x1111: break;                          /* TAG */
            case 0x3003: {                                            /* GOSUB */
                LW(G_ttm_gosub_depth)++;
                LW(G_ttm_gosub_dx) = (u16)a[0];
                LW(G_ttm_gosub_dy) = (u16)a[1];
                bool was0 = LW(G_ttm_purge) == 0;
                ttm_run_frame(ttm_find_tag((u16)a[2]));
                if (LW(G_ttm_purge) && was0) LW(G_ttm_purge) = 0;
                LW(G_ttm_gosub_depth)--;
                break;
            }
            case 0x1201: LW(G_ttm_goto) = (u16)ttm_find_tag((u16)a[0]); break;   /* GOTO (no effect) */
            default:
                if (ap) goto unknown;
                switch (op) {
                case 0x0010: case 0x5005: break;                      /* NOP */
                case 0x0020: if (!skip) ttm_save_bg(); break;         /* SAVE_BG */
                case 0x00E0:                                          /* MTX_CLOSE */
                    if (!skip && LB(G_ttm_mtx_open)) { mtx_close(); LB(G_ttm_mtx_open) = 0; }
                    break;
                case 0xF08F:                                          /* MTX_OPEN */
                    if (skip) break;
                    if (LB(G_ttm_mtx_open)) mtx_close();
                    LB(G_ttm_mtx_open) = (u8)bmp_mtx_load(str);
                    break;
                case 0x2032:                                          /* MTX_SCROLL */
                    if (!skip && LB(G_ttm_mtx_open)) mtx_scroll(a[0], a[1]);
                    break;
                case 0x0400:                                          /* PAL_RESET */
                    if (skip || !DRVB(D_INITS)) break;
                    pal_cycle_block(-1, 0, 0);
                    pending_set(pal_set(pending()));
                    LW(G_ttm_cyc_on) = 0;
                    break;
                case 0x0500: LW(G_ttm_no_restore) = 1; break;
                case 0x0510: LW(G_ttm_no_restore) = 0; break;
                case 0x1011: gfx_entry25((u16)a[0]); break;
                case 0x1021: LW(G_ttm_delay) = (u16)a[0]; break;      /* SET_DELAY */
                case 0x1041: LW(G_ttm_1041) = (u16)a[0]; break;
                case 0x2012:                                          /* HOOK */
                    if (pid() == PROG_PS) ttm_hook(a[0]);
                    else ttm_hook_2012((u16)a[0], (u16)a[1]);
                    break;
                case 0x2022: {                                        /* RAND_DELAY */
                    if (skip) break;
                    u16 r = ttm_rand(), rem;
                    div16_retry(r, (u16)(a[1] - a[0]), &rem);
                    s16 v = (s16)rem;
                    if (v < 0) v = (s16)-v;
                    LW(G_ttm_delay) = (u16)(a[0] + v);
                    break;
                }
                case 0x2302: case 0x2312: case 0x2322:                /* PAL_BLOCK */
                    if (DRVB(D_INITS)) {
                        u16 i = (u16)((op - 0x2302) >> 4);
                        DSW((u16)(lds(G_ttm_cyc_first) + 2 * i)) = (u16)a[0];
                        DSW((u16)(lds(G_ttm_cyc_last) + 2 * i)) = (u16)a[1];
                    }
                    break;
                case 0x2402: {                                        /* PAL_CYCLE */
                    if (skip || !DRVB(D_INITS)) break;
                    if (a[1] == 0) a[1] = 1;
                    s16 ab = a[1] < 0 ? (s16)-a[1] : a[1];
                    s16 dir = (s16)(a[1] / ab);
                    LW(G_ttm_cyc_step) = (u16)ab;
                    LW(G_ttm_cyc_on) = 1;
                    pal_cycle_block(-1, 0, 0);
                    if (a[0] & 1) ttm_cycle_add(0, dir);
                    if (a[0] & 2) ttm_cycle_add(1, dir);
                    if (a[0] & 4) ttm_cycle_add(2, dir);
                    break;
                }
                case 0x3103:                                          /* SCROLL */
                    if (skip) break;
                    ttm_scroll(a[0], a[1], a[2]);
                    ttm_update_rect(0, 0, (u16)scr_w(), (u16)scr_h());
                    break;
                case 0x4114: {                                        /* FADE_OUT */
                    if (skip) break;
                    if (DRVB(D_INITS)) {
                        /* the speed is kept in ttm_op (DS:33C0), as the original */
                        LW(G_ttm_op) = DSW((u16)(lds(G_ttm_fade_speed_tab) + 2 * a[3]));
                        if (LW(G_ttm_op) == 0) pal_fade(a[0], a[1], a[2], 0);
                        else
                            for (s16 v = 0x280; v != 0; v = (s16)(v - LS(G_ttm_op))) fade_lvl(a[0], a[1], a[2], v);
                    }
                    DRVW(D_PAGE_READ) = DRVW(D_PAGE_BACK);
                    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_FRONT);
                    gfx_copy_rect(0, 0, scr_w(), scr_h());
                    if (LS(G_ttm_level) >= 3) {
                        DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_BG);
                        gfx_copy_rect(0, 0, scr_w(), scr_h());
                    }
                    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_BACK);
                    pending_set(pal_set(pending()));
                    break;
                }
                case 0x4124:                                          /* FADE_IN */
                    if (skip) break;
                    pal_set(pending());
                    if (DRVB(D_INITS)) pal_fade(a[0], a[1], a[2], 0);
                    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_FRONT);
                    DRVW(D_PAGE_READ) = DRVW(D_PAGE_BACK);
                    gfx_copy_rect(0, 0, scr_w(), scr_h());
                    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_BACK);
                    if (!DRVB(D_INITS)) break;
                    LW(G_ttm_op) = DSW((u16)(lds(G_ttm_fade_speed_tab) + 2 * a[3]));
                    if (LW(G_ttm_op) == 0) pal_fade(a[0], a[1], a[2], 0x3F);
                    else
                        for (s16 v = 0; v <= 0x280; v = (s16)(v + LS(G_ttm_op))) fade_lvl(a[0], a[1], a[2], v);
                    break;
                case 0x4136: {                                        /* FADE_PART (no skip test) */
                    if (!DRVB(D_INITS)) break;
                    s16 l1 = (s16)(idiv16_retry((s16)(a[3] << 6), 0x65, NULL) & 0x3F);
                    s16 l2 = (s16)(idiv16_retry((s16)(a[4] << 6), 0x65, NULL) & 0x3F);
                    u16 m = stk_mark(), t = stk_alloc(6);
                    ttm_timer_start(t, (s32)a[5]);
                    /* PS: the done test is the shell's 22a0:0009 on the 236.7 Hz clock (§12.5) */
                    while (!(pid() == PROG_PS ? tmr_expired(t) : ttm_timer_done(t))) {
                        pal_fade(a[0], a[1], a[2], ttm_timer_value(t, l1, l2));
                        host_pump();                              /* PORT: the tick counter advances in host_pump */
                    }
                    pal_fade(a[0], a[1], a[2], l2);
                    stk_release(m);
                    break;
                }
                case 0x4204:                                          /* STORE_AREA */
                    if (skip || LS(G_ttm_level) < 3) break;
                    ttm_copy_back_to_bg(a[0], a[1], a[2], a[3]);
                    upd_add((u16)a[0], (u16)a[1], (u16)a[2], (u16)a[3], 1,
                            LW(G_ttm_dblbuf) ? DRVW(D_PAGE_BG) : DRVW(D_PAGE_BACK), DRVW(D_PAGE_FRONT), 0, far_make(0, 0));
                    break;
                case 0xB000: if (!skip) scroller_init(); break;
                case 0xB013: scroller_step((u16)a[0], (u16)a[1], (u16)a[2]); break;
                case 0xB606:                                          /* COPY_AREA */
                    if (skip) break;
                    DRVW(D_PAGE_READ) = ttm_page_by_index(a[4]);
                    DRVW(D_PAGE_WRITE) = ttm_page_by_index(a[5]);
                    gfx_copy_rect(a[0], a[1], a[2], a[3]);
                    DRVW(D_PAGE_READ) = DRVW(D_PAGE_FRONT);
                    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_BACK);
                    break;
                default:
                    goto unknown;
                }
                break;
            unknown:
                if (op >= 0xF10F && op <= 0xF19F) {                  /* SET_STRING */
                    u16 i = (u16)((op - 0xF10F) >> 4);
                    rtl_strcpy(str_slot((int)i), str);
                    for (s16 k = (s16)(i + 1); k < 9; k++) DSB(str_slot(k)) = 0;
                }
                break;
            }
        }
    next:
        if (!ap && LW(G_ttm_op) >= 0xA000 && LW(G_ttm_op) <= 0xAFFF) after_draw(LW(G_ttm_op), a);
    }
    if (!ap && LW(G_ttm_full_dirty)) {
        if (LS(G_ttm_level) >= 3) {
            u8 c = DRVB(D_CLIP_ON);
            DRVB(D_CLIP_ON) = 0;
            upd_add(0, 0, (u16)scr_w(), (u16)scr_h(), 1, LW(G_ttm_dblbuf) ? DRVW(D_PAGE_BG) : DRVW(D_PAGE_BACK),
                    DRVW(D_PAGE_FRONT), 0, far_make(0, 0));
            DRVB(D_CLIP_ON) = c;
        }
        LW(G_ttm_full_dirty) = 0;
    }
    return 1;
}
