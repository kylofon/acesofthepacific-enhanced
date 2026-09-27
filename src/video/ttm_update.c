/* TTM helper modules (video_ttm.md §9, §11, §12): the update list (INTRO 0fca / PS 3a73), the MTX tile
 * map (0f48 / 39bd / AP 26db), the LFSR dissolves (0f88 / 39fd, 1111 / 3aee), the bitmap scroller
 * (0f9e / 3a48), the text box (0fb0 / 3a5a), the RNG (10f5 / 3ae6) and INTRO's empty 2012 hook. */
#include "video/vid_priv.h"

#include "platform/platform.h"
#include "platform/rtl.h"
#include "shell_data/shell_data.h"

/* ------------------------------------------------------------------ common TTM accessors */

static u16 ctx(void) { return LW(G_ttm_ctx); }
static FarPtr rec(void) { return ds_far(lds(G_ttm_rec)); }
static u16 rec_bmp(u16 slot) { FarPtr r = rec(); return rd16(r.seg, (u16)(r.off + 0x0C + 2 * slot)); }
static u16 rec_font(u16 slot) { FarPtr r = rec(); return rd16(r.seg, (u16)(r.off + 0x18 + 2 * slot)); }
static u16 str_slot(int i) { return DSW((u16)(lds(G_ttm_str_slot) + 2 * i)); }
static s16 scr_w(void) { return DRVS(D_SCR_W); }
static s16 scr_h(void) { return DRVS(D_SCR_H); }
static s16 frame_count(u16 bmp) { return pid() == PROG_INTRO ? (s16)intro_bmp_count(bmp) : bmp_count(bmp); }

/* PORT: the dissolves and wipes draw straight into the displayed page; present every so often and pace
 * them a little (the original speed was CPU-bound). */
static void pace(u32 us) { vid_present(); host_wait_until_ns(host_ns() + (u64)us * 1000u); }

/* ======================================================================== update list (§9) */

#define UPD_HEADS (lds(G_upd_heads))
#define UPD_FREE  LW(G_upd_free)
enum { N_X = 0, N_Y = 2, N_W = 4, N_H = 6, N_SRC = 8, N_DST = 0x0A, N_MODE = 0x0C, N_KEY = 0x0E, N_AREA = 0x10,
       N_HEAD = 0x12, N_P = 0x14, N_NEXT = 0x18 };
#define NW(n, o) DSW((u16)((n) + (o)))
#define NS(n, o) DSS((u16)((n) + (o)))

/* INTRO 0fca:0001 / PS 3a73:000d */
s16 upd_alloc_nodes(s16 n)
{
    n = (s16)(idiv16_retry((s32)(s16)(n + 4), 5, NULL) * 5);
    u16 p = rtl_calloc((u16)n, 0x1A);
    if (p == 0) return 0;
    u16 si = p;
    NW(si, N_HEAD) = 1;
    for (s16 i = 1; i < n; i++, si = (u16)(si + 0x1A)) NW(si, N_NEXT) = (u16)(si + 0x1A);
    NW(si, N_NEXT) = UPD_FREE;
    UPD_FREE = p;
    LW(G_upd_nnodes) = (u16)(LW(G_upd_nnodes) + n);
    return 1;
}

/* INTRO 0fca:0584 / PS 3a73:0590: the head of the list (src, dst, key), else the first empty head, or 0 */
u16 upd_find(u16 src, u16 dst, u16 key)
{
    u16 empty = 0, si = UPD_HEADS;
    for (int k = 0; k < 20; k++, si = (u16)(si + 2)) {
        u16 d = DSW(si);
        if (d) {
            if (NW(d, N_KEY) == key && NW(d, N_SRC) == src && NW(d, N_DST) == dst) return si;
        } else if (!empty) empty = si;
    }
    return empty;
}

/* INTRO 0fca:0079 / PS 3a73:0085 — §9 */
void upd_add(u16 x, u16 y, u16 w, u16 h, u16 mode, u16 src, u16 dst, u16 key, FarPtr p)
{
    s16 X = (s16)x, Y = (s16)y, W = (s16)w, H = (s16)h;
    if (mode == 4) src = 0xFFFF;
    u16 L = upd_find(src, dst, key);
    if (!L) return;
    if (mode == 1) {
        if (DRVB(D_CLIP_ON)) {
            if ((X > DRVS(D_CLIP_X2)) | ((s16)(X + W) < DRVS(D_CLIP_X1)) | (Y > DRVS(D_CLIP_Y2)) | ((s16)(Y + H) < DRVS(D_CLIP_Y1))) return;
            if (X < DRVS(D_CLIP_X1)) { W = (s16)(W - (DRVS(D_CLIP_X1) - X)); X = DRVS(D_CLIP_X1); }
            if (Y < DRVS(D_CLIP_Y1)) { H = (s16)(H - (DRVS(D_CLIP_Y1) - Y)); Y = DRVS(D_CLIP_Y1); }
            if ((s16)(X + W - 1) > DRVS(D_CLIP_X2)) W = (s16)(DRVS(D_CLIP_X2) - X + 1);
            if ((s16)(Y + H - 1) > DRVS(D_CLIP_Y2)) H = (s16)(DRVS(D_CLIP_Y2) - Y + 1);
        } else {
            if ((s16)(scr_w() - 1) < X || (s16)(X + W) < 0 || (s16)(scr_h() - 1) < Y || (s16)(Y + H) < 0) return;
            if (X < 0) { W = (s16)(W - (0 - X)); X = 0; }
            if (Y < 0) { H = (s16)(H - (0 - Y)); Y = 0; }
            if ((s16)(X + W - 1) > (s16)(scr_w() - 1)) W = (s16)(scr_w() - 1 - X + 1);
            if ((s16)(Y + H - 1) > (s16)(scr_h() - 1)) H = (s16)(scr_h() - 1 - Y + 1);
        }
        s16 r = (s16)(X % 8);
        W = (s16)((W + r + 7) / 8);
        X = (s16)(X / 8);
    }
    if (W == 0 || H == 0) return;
    if (UPD_FREE == 0 && !upd_alloc_nodes(5)) return;
    u16 si = UPD_FREE;
    UPD_FREE = NW(si, N_NEXT);
    NW(si, N_NEXT) = 0;
    NS(si, N_X) = X; NS(si, N_Y) = Y; NS(si, N_W) = W; NS(si, N_H) = H;
    NW(si, N_MODE) = mode; NW(si, N_SRC) = src; NW(si, N_DST) = dst; NW(si, N_KEY) = key;
    NW(si, N_P) = p.off; NW(si, N_P + 2) = p.seg;
    NS(si, N_AREA) = (s16)(W * H);
    if (mode == 1) {                                             /* merge (02b2..03b6) */
        u16 di = DSW(L), stop = 0, mark = 0, prev = 0, next;
        while (di != stop) {
            stop = mark;
            next = NW(di, N_NEXT);
            s16 sum = (s16)(NS(di, N_AREA) + NS(si, N_AREA));
            s16 ux = NS(di, N_X) < NS(si, N_X) ? NS(di, N_X) : NS(si, N_X);
            s16 a = (s16)(NS(di, N_X) + NS(di, N_W)), b = (s16)(NS(si, N_X) + NS(si, N_W));
            s16 ux2 = a > b ? a : b;
            s16 uy = NS(di, N_Y) < NS(si, N_Y) ? NS(di, N_Y) : NS(si, N_Y);
            a = (s16)(NS(di, N_Y) + NS(di, N_H)); b = (s16)(NS(si, N_Y) + NS(si, N_H));
            s16 uy2 = a > b ? a : b;
            s16 ua = (s16)((s16)(ux2 - ux) * (s16)(uy2 - uy));
            if (!((s16)(sum + 0x14) < ua)) {
                NS(si, N_X) = ux; NS(si, N_Y) = uy; NS(si, N_W) = (s16)(ux2 - ux); NS(si, N_H) = (s16)(uy2 - uy);
                NS(si, N_AREA) = ua;
                if (prev) NW(prev, N_NEXT) = next; else DSW(L) = next;
                NW(di, N_NEXT) = UPD_FREE;
                UPD_FREE = di;
                mark = next;
                stop = prev;
                di = prev;
            }
            prev = di;
            di = next;
            if (di == 0 && stop != 0) { di = DSW(L); prev = 0; }
        }
    }
    NW(si, N_NEXT) = DSW(L);
    DSW(L) = si;
}

/* INTRO 0fca:05ce / PS 3a73:05da */
void upd_flush_list(u16 src, u16 dst, u16 key)
{
    u16 L = upd_find(src, dst, key);
    if (!L) return;
    u16 si = DSW(L);
    if (!si) return;
    DRVW(D_PAGE_READ) = NW(si, N_SRC);
    DRVW(D_PAGE_WRITE) = NW(si, N_DST);
    u16 last = 0;
    for (; si; si = NW(si, N_NEXT)) {
        if (NW(si, N_MODE) == 1)
            gfx_copy_rect((s16)(NW(si, N_X) << 3), NS(si, N_Y), (s16)(NW(si, N_W) << 3), NS(si, N_H));
        else if (NW(si, N_MODE) == 4)
            gfx_put_rect(far_make(NW(si, N_P + 2), NW(si, N_P)), NS(si, N_X), NS(si, N_Y), NS(si, N_W), NS(si, N_H));
        last = si;
    }
    NW(last, N_NEXT) = UPD_FREE;
    UPD_FREE = DSW(L);
    DSW(L) = 0;
}

/* INTRO 0fca:03cc / PS 3a73:03d8 — the pair table resolved at flush time */
void upd_flush(s16 only_front)
{
    u16 sr = DRVW(D_PAGE_READ), sw = DRVW(D_PAGE_WRITE);
    u16 si = (u16)(lds(G_upd_flush_pairs) + (only_front ? 4 : 0));
    while (DSW(si + 2) != 0) {
        upd_flush_list(DSW(DSW(si)), DSW(DSW(si + 2)), 0);
        si = (u16)(si + 4);
        if (only_front) break;
    }
    DRVW(D_PAGE_READ) = sr;
    DRVW(D_PAGE_WRITE) = sw;
    if (only_front) return;
    u16 h = UPD_HEADS;
    for (int k = 0; k < 20; k++, h = (u16)(h + 2))
        for (u16 d = DSW(h); d; d = NW(d, N_NEXT)) NW(d, N_KEY) = (u16)(NW(d, N_KEY) - 1);
}

/* INTRO 0fca:0461 / PS 3a73:046d */
void upd_free_lists(void)
{
    u16 si = UPD_HEADS;
    for (int k = 0; k < 20; k++, si = (u16)(si + 2)) {
        u16 d = DSW(si);
        if (!d) continue;
        while (NW(d, N_NEXT)) d = NW(d, N_NEXT);
        NW(d, N_NEXT) = UPD_FREE;
        UPD_FREE = DSW(si);
        DSW(si) = 0;
    }
}

/* INTRO 0fca:049b / PS 3a73:04a7 (no caller): mode of the first mode-1 node overlapping the rectangle */
s16 upd_query(s16 x, s16 y, s16 w, s16 h, u16 dst, u16 key)
{
    s16 cx = (s16)((w + x % 8 + 7) / 8);
    s16 di = (s16)(x / 8);
    u16 hp = UPD_HEADS;
    for (int k = 0; k < 20; k++, hp = (u16)(hp + 2)) {
        u16 si = DSW(hp);
        if (!si || NW(si, N_DST) != dst || NW(si, N_KEY) != key) continue;
        for (; si; si = NW(si, N_NEXT))
            if (NW(si, N_MODE) == 1 && NS(si, N_X) < (s16)(di + cx) && (s16)(NS(si, N_X) + NS(si, N_W)) > di &&
                NS(si, N_Y) < (s16)(y + h) && (s16)(NS(si, N_Y) + NS(si, N_H)) > y)
                return NS(si, N_MODE);
    }
    return 0;
}

/* INTRO 0fca:0543 / PS 3a73:054f — frees the node blocks (recursively, as the code does) */
void upd_free_all(void)
{
    upd_free_lists();
    for (u16 si = UPD_FREE; si; si = NW(si, N_NEXT)) {
        if (NW(si, N_HEAD) & 1) {
            NW(si, N_HEAD) = 0;
            upd_free_all();
            rtl_free(si);
            break;
        }
    }
    UPD_FREE = 0;
}

/* INTRO 0fca:057a / PS 3a73:0586 */
s16 upd_count(void) { return LS(G_upd_nnodes); }

/* INTRO 0fca:0679 / PS 3a73:0685 — frees a list without copying */
void upd_discard(u16 src, u16 dst, u16 key)
{
    u16 L = upd_find(src, dst, key);
    if (!L || !DSW(L)) return;
    u16 d = DSW(L);
    while (NW(d, N_NEXT)) d = NW(d, N_NEXT);
    NW(d, N_NEXT) = UPD_FREE;
    UPD_FREE = DSW(L);
    DSW(L) = 0;
}

/* INTRO 0fca:06b9 / PS 3a73:06c5 (no caller) — re-adds a list into another. Faithful to the bug: the
 * node pointer is never advanced, so a non-empty list loops forever. */
void upd_move_list(u16 src, u16 dst, u16 key, u16 src2, u16 dst2, u16 key2)
{
    u16 a = upd_find(src, dst, key);
    if (upd_find(src2, dst2, key2) == a || !a || !DSW(a)) return;
    u16 si = DSW(a);
    while (si)
        upd_add((u16)(NW(si, N_X) << 3), NW(si, N_Y), (u16)(NW(si, N_W) << 3), NW(si, N_H), NW(si, N_MODE), src2, dst2, key2, far_make(0, 0));
}

/* ======================================================================== RNG (§12.4) */

/* INTRO 10f5:0009 / PS 3ae6:000c */
u16 ttm_rand(void)
{
    u16 t = lds(G_rnd_tab);
    u16 j = LW(G_rnd_j), k = LW(G_rnd_k);
    DSW((u16)(t + 2 * k)) = (u16)(DSW((u16)(t + 2 * k)) + DSW((u16)(t + 2 * j)));
    u16 r = DSW((u16)(t + 2 * k));
    if (++LW(G_rnd_k) == 0x38) LW(G_rnd_k) = 0;
    if (++LW(G_rnd_j) == 0x38) LW(G_rnd_j) = 0;
    return r;
}

/* INTRO 10f5:0056 / PS 3ae6:0059 (no caller) */
s16 ttm_rand_skip(s16 n)
{
    for (s16 i = 0; i < n; i++) ttm_rand();
    return n;
}

/* INTRO 10f5:006b / PS 3ae6:006e */
void rnd_randomize(void) { ttm_rand_skip((s16)rtl_time(0)); }

/* ======================================================================== MTX tile map (§12.1) */

#define MTX_ROW(r) DSW((u16)(lds(G_mtx_rows) + 2 * (r)))

/* INTRO 0f48:023d / PS 39bd:023d / AP 26db:023a */
void mtx_free_row(s16 r)
{
    if (MTX_ROW(r)) { rtl_free(MTX_ROW(r)); MTX_ROW(r) = 0; }
}

/* INTRO 0f48:026a / PS 39bd:026a / AP 26db:0267 */
void mtx_free_rows(void)
{
    for (s16 i = 0; i < LS(G_mtx_nrows); i++) mtx_free_row(i);
}

/* INTRO 0f48:0282 / PS 39bd:0282 / AP 26db:027f */
void mtx_close(void)
{
    mtx_free_rows();
    u8 a = DRVB(D_ADAPTER);
    if (a == 8 || a == 2) ((DrvWord1Fn)drv_entry(32))(0);
    else if (a == 1 || a == 3) dos_free(far_make(DSW(lds(G_mtx_frame)), DSW(LW(G_mtx_offsets))));
    rtl_free(LW(G_mtx_offsets));
}

/* INTRO 0f48:000c mtx_open is platform's bmp_mtx_load (port/modules.csv). */

/* INTRO 0f48:02d3 / PS 39bd:02d3 / AP 26db:02d0 — tiles from the cache (px steps by 8, not tw: quirk) */
void mtx_draw(u16 x, u16 y, u16 w, u16 h)
{
    s16 tw = LS(G_mtx_tw), th = LS(G_mtx_th);
    if (!tw || !th) return;
    s16 nc = (s16)((s16)w / tw), nr = (s16)((s16)h / th);
    for (s16 r = 0; r < nr; r++) {
        s16 rr = (s16)((r + LS(G_mtx_scroll_y)) % LS(G_mtx_nrows));
        if (rr < 0) rr = (s16)(rr + LS(G_mtx_nrows));
        u16 row = MTX_ROW(rr);
        for (s16 c = 0; c < nc; c++) {
            s16 cc = (s16)((c + LS(G_mtx_scroll_x)) % LS(G_mtx_ncols));
            if (cc < 0) cc = (s16)(cc + LS(G_mtx_ncols));
            u16 t = DSW((u16)(row + 2 * cc));
            s16 px = (s16)((s16)x + c * 8);
            if (px <= 0 || px >= LS(G_mtx_lim_x)) continue;
            s16 py = (s16)((s16)y + r * th);
            if (py <= 0 || py >= LS(G_mtx_lim_y)) continue;
            DSW(lds(G_mtx_frame) + 2) = DSW((u16)(LW(G_mtx_offsets) + 2 * t));
            gfx_cache_blit(lds(G_mtx_frame), px, py);
        }
    }
}

/* INTRO 0f48:03d1 / PS 39bd:03d1 / AP 26db:03ce */
void mtx_scroll(s16 dir, s16 n)
{
    switch (dir) {
    case 0: LW(G_mtx_scroll_y) = (u16)(LW(G_mtx_scroll_y) - n); break;
    case 1: LW(G_mtx_scroll_y) = (u16)(LW(G_mtx_scroll_y) + n); break;
    case 2: LW(G_mtx_scroll_x) = (u16)(LW(G_mtx_scroll_x) - n); break;
    case 3: LW(G_mtx_scroll_x) = (u16)(LW(G_mtx_scroll_x) + n); break;
    default: break;
    }
}

/* ======================================================================== dissolves (§11) */

static s16 bitlen16(u16 v) { s16 n = 0; while (v) { n++; v >>= 1; } return n; }

/* INTRO 0f88:000a / PS 39fd:000a — LFSR dissolve back -> front through entries 21/22 */
s16 ttm_dissolve(u16 x, u16 y, u16 w, u16 h)
{
    s16 di = (s16)(bitlen16(w) - 2);
    u32 ymask = 0;
    s16 yb = 0;
    for (u16 s = h; s; s >>= 1) { ymask = ymask * 2 + 1; di++; yb++; }
    u32 taps = DSL((u16)(lds(G_lfsr_taps32) + 4 * di));
    u32 s = 1;
    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_FRONT);
    DRVW(D_PAGE_READ) = DRVW(D_PAGE_BACK);
    u32 n = 0;
    do {
        u32 py = s & ymask;
        if (!((py >> 16) != 0 || (u16)py > (u16)(h - 1))) {
            u16 px = (u16)(s >> yb);
            if (!(px > (u16)(w - 1))) {
                s16 X = (s16)(px + x), Y = (s16)((u16)py + y);
                ((DrvPutPixelFn)drv_entry(22))(X, Y, ((DrvGetPixelFn)drv_entry(21))(X, Y));
                if ((++n & 1023) == 0) pace(2000);
            }
        }
        s = (s & 1) ? (s >> 1) ^ taps : s >> 1;
    } while (s != 1);
    ((DrvPutPixelFn)drv_entry(22))((s16)x, (s16)y, ((DrvGetPixelFn)drv_entry(21))((s16)x, (s16)y));
    vid_present();
    return 1;
}

/* INTRO 1111:0001 / PS 3aee:0002 — 32 timed batches (PS waits on the shell timer 22a0:0009) */
void ttm_dissolve_timed(u16 x, u16 y, u16 w, u16 h, u16 dur)
{
    u16 n = (u16)(w * h);
    u16 per = n >> 5; if (!per) per = 1;
    DRVW(D_PAGE_READ) = DRVW(D_PAGE_BACK);
    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_FRONT);
    if (DRVB(D_INITS) == 0) { gfx_copy_rect((s16)x, (s16)y, (s16)w, (s16)h); return; }
    s16 dt = (s16)((s16)dur >> 5); if (!dt) dt = 1;
    s16 bi = (s16)(bitlen16(n) - 2);
    u16 taps = DSW((u16)(lds(G_lfsr_taps16) + 4 * bi));
    u16 s = 1, c = per;
    u16 m = stk_mark(), t = stk_alloc(6);
    ttm_timer_start(t, dt);
    do {
        if (s < n) {
            s16 X = (s16)(x + s % w), Y = (s16)(y + s / w);
            ((DrvPutPixelFn)drv_entry(22))(X, Y, ((DrvGetPixelFn)drv_entry(21))(X, Y));
            if (--c == 0) {
                c = per;
                vid_present();
                while (!(pid() == PROG_PS ? tmr_expired(t) : ttm_timer_done(t))) host_pump();
                ttm_timer_start(t, dt);
            }
        }
        s = (s & 1) ? (u16)((s >> 1) ^ taps) : (u16)(s >> 1);
    } while (s != 1);
    ((DrvPutPixelFn)drv_entry(22))((s16)x, (s16)y, ((DrvGetPixelFn)drv_entry(21))((s16)x, (s16)y));
    stk_release(m);
    vid_present();
}

/* ======================================================================== scroller (§12.2) */

/* INTRO 0f9e:0000 / PS 3a48:0001 */
void scroller_init(void)
{
    LW(G_scroller_maxw) = 0;
    LW(G_scroller_pos) = 0;
    u16 h = rec_bmp(DSW(ctx() + 0x14));
    for (s16 i = 0; frame_count(h) > i; i++) {
        s16 w = DSS(DSW((u16)(h + 2 * i)) + 6);
        if (!(w < LS(G_scroller_maxw))) LW(G_scroller_maxw) = (u16)w;
    }
}

/* INTRO 0f9e:0058 / PS 3a48:0059 — vertical credits scroller into the back page */
void scroller_step(u16 gap, u16 speed, u16 x)
{
    u16 h = rec_bmp(DSW(ctx() + 0x14));
    if (!h) return;
    s16 n = frame_count(h);
    if (!n) return;
    s16 y = (s16)(scr_h() - LS(G_scroller_pos));
    u16 none = 1;
    for (s16 i = 0; i < n; i++) {
        if (y > scr_h()) break;
        u16 fr = DSW((u16)(h + 2 * i));
        s16 fw = DSS(fr + 6), fh = DSS(fr + 8);
        if ((s16)-LS(G_scroller_maxw) < y) {
            bmp_draw(fr, (s16)x, y, 0);
            ttm_update_rect(x, (u16)y, (u16)fw, (u16)fh);
            none = 0;
        }
        y = (s16)(y + (s16)gap + fh);
    }
    LW(G_scroller_pos) = (u16)(LW(G_scroller_pos) + speed);
    LW(G_ttm_purge) |= none;
}

/* ======================================================================== text box (§12.3) */

/* INTRO 0fb0:000c / PS 3a5a:000d */
void ttm_textbox(u16 x_, u16 y_, u16 w_, u16 h_)
{
    s16 x = (s16)x_, y = (s16)y_, w = (s16)w_, h = (s16)h_;
    u16 f = rec_font(DSW(ctx() + 0x10));
    gfx_font_select((s16)f);
    s16 sw = gfx_text_width(lds(G_ttm_space_str));
    s16 fh = gfx_font_height((s16)f);
    if (!sw || !fh) return;
    DRVB(D_DRAW_COL) = 0;
    DRVB(D_FILL_COL) = DSB(ctx() + 0x1F);
    gfx_rect(x, y, (s16)(w - 3), (s16)(fh + 3));
    DRVB(D_FILL_COL) = DSB(ctx() + 0x1E);
    gfx_rect(x, (s16)(y + fh + 3), (s16)(w - 3), (s16)(h - fh - 6));
    DRVB(D_FILL_COL) = 0;
    gfx_rect((s16)(x + w - 3), (s16)(y + 3), 3, (s16)(h - 3));
    gfx_rect((s16)(x + 3), (s16)(y + h - 3), (s16)(w - 3), 3);
    DRVB(D_TEXT_FLAGS) = 1;
    DRVB(D_TEXT_FG) = 0;
    s16 tx = (s16)(x + ((s16)(w - gfx_text_width(str_slot(0))) >> 1) + sw - 1);
    for (int i = 0; i <= 8; i++) {
        if (!((s16)(gfx_text_width(str_slot(i)) + sw) < w)) continue;
        if (i == 0) gfx_text(str_slot(0), tx, (s16)(y + 2));
        else if ((s16)((i + 2) * fh) < h) gfx_text(str_slot(i), (s16)(x + sw), (s16)(y + i * fh + 5));
    }
    DRVB(D_TEXT_FLAGS) = 0;
}

/* INTRO 0fc9:000c — the 2012 hook of INTRO: empty */
void ttm_hook_2012(u16 a0, u16 a1) { (void)a0; (void)a1; }
