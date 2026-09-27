/* sim_flow: cockpit window outlines .EDG and the masked driver vectors (0962), the span-list
 * intersection (0a7b) — sim_flow_cockpit.md §4. While the mask is on (mask_on), six video vectors
 * point at the routines below (HANDOFFS C6: far pointers 0962:xxxx written into the driver jump table
 * DS:351A and the vectors DS:3436 / DS:31E0, called through codeptr_lookup). Everything is a literal
 * transcription of the disassembly, quirks included (see the comments). */
#include "sim_flow/sfa_priv.h"

/* edge arena */
#define ARENA      0x142E        /* far base */
#define ARENA_CAP  0x1432
#define ARENA_CUR  0x1434        /* far cursor */
#define ARENA_USED 0x1438
#define MARK_CUR   0x143A
#define MARK_USED  0x143E
#define MASK_DEPTH 0x1440
/* loaded .EDG: A = rows fully open, B = window polygons (+ x bounds), C = overlay polygons */
#define A_PTR   0x5674
#define A_N     0x5678
#define B_PTRS  0x566E
#define B_BOUND 0x566A
#define B_N     0x5672
#define C_PTRS  0x5664
#define C_N     0x5668
/* saved vectors */
#define SV_SCN   0x564C          /* DS:31E0 scn_draw_fn */
#define SV_ROT   0x5650          /* DS:3436 rot_fn */
#define SV_SPR   0x5654          /* entry 29 (DS:358E) */
#define SV_LINE  0x5658          /* entry 2  (DS:3522) */
#define SV_PIX   0x565C          /* entry 22 (DS:3572) */
#define SV_POLY  0x5660          /* entry 27 (DS:3586) */

typedef void (*PolyFn)(FarPtr);
typedef void (*PixFn)(s16, s16, u16);
typedef void (*SprFn)(u16, s16, s16, u16);
typedef void (*ScnFn)(u16, s16, s16, s16);
typedef void (*RotFn)(u16, s16, s16, u16, s16, s16, s16, s16);

static s16 fr16(FarPtr p, u16 o) { return (s16)rd16(p.seg, (u16)(p.off + o)); }
static void fw16(FarPtr p, u16 o, u16 v) { wr16(p.seg, (u16)(p.off + o), v); }
static FarPtr far_at(FarPtr arr, s16 i) { return far_rd(arr.seg, (u16)(arr.off + 4 * i)); }

static void call_poly(FarPtr spans) { ((PolyFn)codeptr_lookup(ds_far(SV_POLY)))(spans); }

/* AP 0962:0009 edge_arena_init(size) */
void edge_arena_init(s16 size)
{
    FarPtr p = mem_far_new((u32)(s32)size, 0);
    ds_far_wr(ARENA, p);
    DSW(ARENA_CAP) = (u16)size;
    DSL(ARENA_CUR) = DSL(ARENA);
    DSW(ARENA_USED) = 0;
    ds_far_wr(0x567A, edge_alloc(0x340));
    DSW(0x5680) = DSW(AP_DS_drv_span_seg);
    DSW(0x567E) = 0;
}

/* AP 0962:005a edge_arena_free */
void edge_arena_free(void)
{
    if (DSL(ARENA) != 0) mem_far_delete(ds_far(ARENA));
    DSL(ARENA) = 0;
    DSW(ARENA_CAP) = 0;
    DSL(ARENA_CUR) = 0;
    DSW(ARENA_USED) = 0;
}

/* AP 0962:0095 edge_alloc(n) -> far (0 when full) */
FarPtr edge_alloc(s16 n)
{
    if ((s16)(DSW(ARENA_USED) + n) > DSS(ARENA_CAP)) return far_make(0, 0);
    FarPtr p = ds_far(ARENA_CUR);
    DSW(ARENA_CUR) = (u16)(DSW(ARENA_CUR) + n);
    DSW(ARENA_USED) = (u16)(DSW(ARENA_USED) + n);
    return p;
}

/* AP 0962:00cb edge_mark */
void edge_mark(void)
{
    DSL(MARK_CUR) = DSL(ARENA_CUR);
    DSW(MARK_USED) = DSW(ARENA_USED);
}

/* AP 0962:00de edge_release */
void edge_release(void)
{
    DSL(ARENA_CUR) = DSL(MARK_CUR);
    DSW(ARENA_USED) = DSW(MARK_USED);
}

/* read a polygon list (count at cnt_ds, far pointer array at ptrs_ds, optional bounds) */
static void edge_read_polys(u16 f, u16 cnt_ds, u16 ptrs_ds, u16 bounds_ds)
{
    u16 m = stk_mark();
    u16 hdr = stk_alloc(4);
    my_fread(cnt_ds, 2, 1, f);
    if (DSW(cnt_ds) == 0) { stk_release(m); return; }
    ds_far_wr(ptrs_ds, edge_alloc((s16)(DSW(cnt_ds) << 2)));
    if (bounds_ds) ds_far_wr(bounds_ds, edge_alloc((s16)(DSW(cnt_ds) << 2)));
    for (s16 i = 0; i < DSS(cnt_ds); i++) {
        my_fread(hdr, 4, 1, f);
        s16 n = DSS(hdr + 2);
        FarPtr arr = ds_far(ptrs_ds);
        FarPtr poly = edge_alloc((s16)((n + 1) * 4));
        far_wr(arr.seg, (u16)(arr.off + 4 * i), poly);
        wr32(poly.seg, poly.off, DSL(hdr));
        poly = far_at(ds_far(ptrs_ds), i);
        ap_fread_far_55a4(far_add(poly, 4), (s32)(u16)(n << 2), f);
        if (!bounds_ds) continue;
        s16 mn = DSS(AP_DS_drv_scr_w), mx = 0;
        FarPtr s = far_add(far_at(ds_far(ptrs_ds), i), 4);
        for (s16 k = 0; k < n; k++, s = far_add(s, 4)) {
            if (fr16(s, 0) < mn) mn = fr16(s, 0);
            if (mx < fr16(s, 2)) mx = fr16(s, 2);
        }
        FarPtr b = ds_far(bounds_ds);
        wr16(b.seg, (u16)(b.off + 4 * i), (u16)mn);
        wr16(b.seg, (u16)(b.off + 4 * i + 2), (u16)mx);
    }
    stk_release(m);
}

/* AP 0962:00f1 edge_load(name) */
void edge_load(u16 name_ds)
{
    edge_mark();
    u16 f = my_fopen(res_name(name_ds, 0x05F6 /* "EDG" */), 0x05EB /* "rb" */);
    my_fread(A_N, 2, 1, f);
    if (DSW(A_N) != 0) {
        ds_far_wr(A_PTR, edge_alloc((s16)(DSW(A_N) << 2)));
        ap_fread_far_55a4(ds_far(A_PTR), (s32)(u16)(DSW(A_N) << 2), f);
    }
    edge_read_polys(f, B_N, B_PTRS, B_BOUND);
    edge_read_polys(f, C_N, C_PTRS, 0);
    my_fclose(f);
}

/* AP 0962:038d edge_unload */
void edge_unload(void)
{
    edge_release();
}

/* AP 0962:0396 file_size(name) -> the arena bytes the .EDG needs (0 when missing) */
s16 file_size(u16 name_ds)
{
    u16 m = stk_mark();
    u16 w = stk_alloc(2), hdr = stk_alloc(4);
    s16 total = 0;
    u16 f = my_fopen(res_name(name_ds, 0x05F6 /* "EDG" */), 0x05EB /* "rb" */);
    if (!f) { stk_release(m); return 0; }
    my_fread(w, 2, 1, f);
    if (DSW(w) != 0) {
        total = (s16)(DSW(w) << 2);
        my_fseek(f, (s32)(u16)(DSW(w) << 2), 1);
    }
    my_fread(w, 2, 1, f);
    if (DSW(w) != 0) {
        total = (s16)(total + DSS(w) * 8);
        for (s16 i = 0; i < DSS(w); i++) {
            my_fread(hdr, 4, 1, f);
            total = (s16)(total + (DSS(hdr + 2) + 1) * 4);
            my_fseek(f, (s32)(u16)(DSW(hdr + 2) << 2), 1);
        }
    }
    my_fread(w, 2, 1, f);
    if (DSW(w) != 0) {
        total = (s16)(total + DSS(w) * 4);
        for (s16 i = 0; i < DSS(w); i++) {
            my_fread(hdr, 4, 1, f);
            total = (s16)(total + (DSS(hdr + 2) + 1) * 4);
            my_fseek(f, (s32)(u16)(DSW(hdr + 2) << 2), 1);
        }
    }
    my_fclose(f);
    stk_release(m);
    return total;
}

/* AP 0a7b:000c span_intersect(a_seg, a_off, b_off, far out) � asm: both lists are read in segment
 * a_seg; rows are bytes. Row-wise intersection written to out+4.., header at out; empty rows after the
 * first non-empty one become (1, 0); trailing empty rows are dropped from the count. -> out or 0.
 * PORT: b_seg is the list's own segment. The original reads the incoming list at a_seg:b_off, but the
 * library polygon fill passes it as (span_seg - 1):(4*y0 + 0Ch) (1d32:0d97), 16 bytes before a_seg:b_off,
 * so the original takes a span of row y0+3 as the header and can run past the 330h-byte output buffer
 * (it corrupts the DOS arena here); the port reads the list the caller passed. */
FarPtr span_intersect(u16 a_seg, u16 a_off, u16 b_seg, u16 b_off, FarPtr out)
{
    u16 si = a_off, di = b_off;
    u16 ay = rd16(a_seg, si);
    u8 dl = (u8)ay, dh = (u8)(ay + rd16(a_seg, (u16)(si + 2)) - 1);
    si = (u16)(si + 4);
    u16 by = rd16(b_seg, di);
    u8 cl = (u8)by, ch = (u8)(by + rd16(b_seg, (u16)(di + 2)) - 1);
    di = (u16)(di + 4);
    if (dl > ch || cl > dh) return far_make(0, 0);
    s16 d = (s16)(((s16)dl - (s16)cl) << 2);
    u8 al;
    if (d >= 0) { di = (u16)(di + d); al = dl; }
    else { si = (u16)(si - d); al = cl; }
    u8 ah = dh > ch ? ch : dh;
    ah = (u8)(ah - al + 1);
    u16 bx = (u16)(out.off + 4);
    cl = al;
    u8 started = 0;
    for (;;) {
        u16 x0 = rd16(a_seg, si), x1 = rd16(a_seg, (u16)(si + 2));
        if (x0 <= rd16(b_seg, di)) x0 = rd16(b_seg, di);
        if (x1 >= rd16(b_seg, (u16)(di + 2))) x1 = rd16(b_seg, (u16)(di + 2));
        if (x1 < x0) {
            if (!started) goto next;                           /* leading empty rows: skipped */
            wr16(out.seg, (u16)(bx + 2), 0);
            wr16(out.seg, bx, 1);
            goto adv;                                          /* (al keeps the last non-empty row) */
        }
        wr16(out.seg, bx, x0);
        wr16(out.seg, (u16)(bx + 2), x1);
        if (!started) {
            wr16(out.seg, (u16)(bx - 4), cl);                  /* header y0 = this row */
            started = 1;
        }
        al = cl;
    adv:
        bx = (u16)(bx + 4);
    next:
        si = (u16)(si + 4);
        di = (u16)(di + 4);
        cl++;
        if (--ah == 0) break;
    }
    if (!started) return far_make(0, 0);
    wr16(out.seg, (u16)(out.off + 2), (u16)(al - rd16(out.seg, out.off) + 1));
    return out;
}

/* AP 0962:0502 span_intersect_w(far a, far b, far out) � the original drops b's segment (see above) */
FarPtr span_intersect_w(FarPtr a, FarPtr b, FarPtr out)
{
    return span_intersect(a.seg, a.off, b.seg, b.off, out);
}

/* AP 0962:0520 span_in_rows(far spans) -> 1 when the list lies wholly in one A range.
 * Quirk kept: a partly covered range advances the list pointer for the following ranges. */
s16 span_in_rows(FarPtr sp)
{
    s16 whole = 0;
    FarPtr a = ds_far(A_PTR);
    for (s16 i = 0; i < DSS(A_N); i++, a = far_add(a, 4)) {
        s16 y0 = fr16(sp, 0);
        s16 y1 = (s16)(y0 + fr16(sp, 2) - 1);
        if (fr16(a, 0) > y1 || fr16(a, 2) < y0) continue;
        s16 d = (s16)(fr16(a, 0) - y0);
        if (d > 0) sp.off = (u16)(sp.off + d * 4);
        if (fr16(a, 0) <= y0 && fr16(a, 2) >= y1) whole = 1;
        y0 = fr16(a, 0) > y0 ? fr16(a, 0) : y0;
        y1 = fr16(a, 2) < y1 ? fr16(a, 2) : y1;
        u16 s0 = rd16(sp.seg, sp.off), s1 = rd16(sp.seg, (u16)(sp.off + 2));
        fw16(sp, 0, (u16)y0);
        fw16(sp, 2, (u16)(y1 - y0 + 1));
        call_poly(sp);
        fw16(sp, 0, s0);
        fw16(sp, 2, s1);
        if (whole) return 1;
    }
    return 0;
}

/* AP 0962:0655 glass_overlay — the C polygons, clipped to the clip rows */
void glass_overlay(void)
{
    if (!DSB(0x26C3)) {
        for (s16 i = 0; i < DSS(C_N); i++) gfx_span_copy(far_at(ds_far(C_PTRS), i));
        return;
    }
    for (s16 i = 0; i < DSS(C_N); i++) {
        FarPtr p = far_at(ds_far(C_PTRS), i);
        s16 y0 = fr16(p, 0);
        s16 n = fr16(p, 2);
        s16 y1 = (s16)(y0 + n - 1);
        if (y0 > DSS(0x26CA) || y1 < DSS(0x26C8)) continue;
        if (y0 >= DSS(0x26C8) && y1 <= DSS(0x26CA)) {
            gfx_span_copy(p);
        } else if (y0 < DSS(0x26C8)) {
            FarPtr q = far_add(p, (u16)((DSS(0x26C8) - y0) << 2));
            u32 save = rd32(q.seg, q.off);
            fw16(q, 0, DSW(0x26C8));
            s16 e = DSS(0x26CA) < y1 ? DSS(0x26CA) : y1;
            fw16(q, 2, (u16)(e - DSS(0x26C8) + 1));
            gfx_span_copy(q);
            wr32(q.seg, q.off, save);
        } else {
            u16 save = rd16(p.seg, (u16)(p.off + 2));
            fw16(p, 2, (u16)(DSS(0x26CA) - y0 + 1));
            gfx_span_copy(p);
            fw16(p, 2, save);
        }
    }
}

/* AP 0962:079a span_through_windows(ES:SI spans) — replaces driver entry 27 */
void span_through_windows(FarPtr sp)
{
    if (span_in_rows(sp)) return;
    for (s16 i = 0; i < DSS(B_N); i++) {
        FarPtr r = span_intersect_w(far_at(ds_far(B_PTRS), i), sp, ds_far(0x567E));
        if (!far_is_null(r)) call_poly(r);
    }
}

/* AP 0962:0819 pixel_rows(x, y, c) — replaces driver entry 22 */
void pixel_rows(s16 x, s16 y, u16 c)
{
    FarPtr a = ds_far(A_PTR);
    for (s16 i = 0; i < DSS(A_N); i++, a = far_add(a, 4)) {
        if (fr16(a, 0) <= y && fr16(a, 2) >= y) {
            ((PixFn)codeptr_lookup(ds_far(SV_PIX)))(x, y, c);
            return;
        }
    }
    for (s16 i = 0; i < DSS(B_N); i++) {
        FarPtr p = far_at(ds_far(B_PTRS), i);
        s16 y0 = fr16(p, 0), y1 = (s16)(y0 + fr16(p, 2) - 1);
        p = far_add(p, 4);
        if (y < y0 || y > y1) continue;
        p = far_add(p, (u16)((y - y0) << 2));
        if (fr16(p, 0) > x || fr16(p, 2) < x) continue;
        ((PixFn)codeptr_lookup(ds_far(SV_PIX)))(x, y, c);
        return;
    }
}

/* AP 0962:08de masked_span_fill — replaces driver entry 2 (line, registers BX x1, CX y1, DX x2,
 * SI y2): rasterises the line into a span list at DS:3172:0000 (the arena while masked) and draws it
 * through span_through_windows with the line colour as the fill colour. Quirks kept: the row count is
 * y2 - y1 (the last row is not drawn), a one-pixel line returns without restoring DS:26CD, the shallow
 * case ends without its last run. TODO(verify): the original's `cdq` abs() uses the stale upper
 * half of EAX; modelled as a 16-bit abs. */
void masked_span_fill(s16 bx_x1, s16 cx_y1, s16 dx_x2, s16 si_y2)
{
    s16 x1 = bx_x1, y1 = cx_y1, x2 = dx_x2, y2 = si_y2;
    s16 adx = (s16)(x1 - x2); if (adx < 0) adx = (s16)-adx;
    s16 ady = (s16)(y1 - y2); if (ady < 0) ady = (s16)-ady;
    u16 seg = DSW(AP_DS_drv_span_seg);
    u16 p = 0;
    if (y1 > y2) { s16 t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    wr16(seg, p, (u16)y1); p += 2;
    u16 n = (u16)(y2 - y1);
    wr16(seg, p, n); p += 2;
    if (n == 0) wr16(seg, (u16)(p - 2), 1);
    u8 save_fill = DSB(0x26CD);
    DSB(0x26CD) = DSB(0x26CE);
    if (ady == 0) {
        if (adx == 0) {
            pixel_rows(x1, y1, (u16)(u8)DSB(0x26CE));
            return;                                          /* (DS:26CD not restored) */
        }
        s16 mn = x1 < x2 ? x1 : x2;
        wr16(seg, p, (u16)mn); p += 2;
        wr16(seg, p, (u16)(mn + adx)); p += 2;
    } else if (adx == 0) {
        while (ady--) { wr16(seg, p, (u16)x1); p += 2; wr16(seg, p, (u16)x1); p += 2; }
    } else if (adx == ady) {
        s16 step = x2 > x1 ? 1 : -1;
        while (ady--) { wr16(seg, p, (u16)x1); p += 2; wr16(seg, p, (u16)x1); p += 2; x1 = (s16)(x1 + step); }
    } else if (ady > adx) {
        s16 err = (s16)(2 * adx - ady), inc1 = (s16)(2 * adx), inc2 = (s16)(2 * (adx - ady));
        s16 step = x2 > x1 ? 1 : -1;
        s16 x = x1;
        while (ady--) {
            wr16(seg, p, (u16)x); p += 2;
            wr16(seg, p, (u16)x); p += 2;
            if (err < 0) err = (s16)(err + inc1);
            else { x = (s16)(x + step); err = (s16)(err + inc2); }
        }
    } else {
        s16 err = (s16)(2 * ady - adx), inc1 = (s16)(2 * ady), inc2 = (s16)(2 * (ady - adx));
        s16 ystart = y1, cnt = 0;
        if (x1 > x2) { s16 t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
        s16 x = x1;
        s16 dir = y2 > y1 ? 1 : -1;
        if (dir == -1) p = (u16)(p + ady * 4);
        s16 start = x;
        while (adx--) {
            if (err < 0) {
                err = (s16)(err + inc1);
            } else {
                wr16(seg, p, (u16)start); p += 2;
                wr16(seg, p, (u16)x); p += 2;
                if (dir == -1) { cnt++; p = (u16)(p - 8); start = (s16)(x + 1); }
                else start = x;
                err = (s16)(err + inc2);
            }
            x++;
        }
        if (dir == -1) {
            if (cnt == 0) return;                            /* (DS:26CD not restored) */
            wr16(seg, p, (u16)ystart);
            wr16(seg, (u16)(p + 2), (u16)cnt);
            span_through_windows(far_make(seg, p));
            DSB(0x26CD) = save_fill;
            return;
        }
    }
    span_through_windows(far_make(seg, 0));
    DSB(0x26CD) = save_fill;
}

/* AP 0962:0bf1 mask_on — nests (DS:1440) */
void mask_on(void)
{
    u16 d = DSW(MASK_DEPTH);
    DSW(MASK_DEPTH) = (u16)(d + 1);
    if (d) return;
    DSL(SV_POLY) = DSL(0x3586);
    DSL(SV_PIX) = DSL(0x3572);
    DSL(SV_LINE) = DSL(0x3522);
    DSL(SV_SPR) = DSL(0x358E);
    DSL(SV_ROT) = DSL(0x3436);
    DSL(SV_SCN) = DSL(0x31E0);
    ds_far_wr(0x3586, far_make(SEG(0x0962), 0x079A));
    ds_far_wr(0x3572, far_make(SEG(0x0962), 0x0819));
    ds_far_wr(0x3522, far_make(SEG(0x0962), 0x08DE));
    ds_far_wr(0x358E, far_make(SEG(0x0962), 0x0CC6));
    ds_far_wr(0x3436, far_make(SEG(0x0962), 0x0E47));
    ds_far_wr(0x31E0, far_make(SEG(0x0962), 0x1023));
    DSW(AP_DS_drv_span_seg) = DSW(0x567C);
}

/* AP 0962:0c7f mask_off */
void mask_off(void)
{
    DSW(MASK_DEPTH) -= 1;
    if (DSW(MASK_DEPTH) != 0) return;
    DSW(MASK_DEPTH) = 0;
    DSL(0x3586) = DSL(SV_POLY);
    DSL(0x3572) = DSL(SV_PIX);
    DSL(0x3522) = DSL(SV_LINE);
    DSL(0x358E) = DSL(SV_SPR);
    DSL(0x3436) = DSL(SV_ROT);
    DSL(0x31E0) = DSL(SV_SCN);
    DSW(AP_DS_drv_span_seg) = DSW(0x5680);
}

/* the B polygons whose bounds meet the box: draw with that polygon as the clip-span list */
static s16 poly_hits(s16 i, s16 x0, s16 y0, s16 x1, s16 y1)
{
    FarPtr b = ds_far(B_BOUND);
    s16 bx0 = fr16(b, (u16)(4 * i)), bx1 = fr16(b, (u16)(4 * i + 2));
    FarPtr p = far_at(ds_far(B_PTRS), i);
    s16 py0 = fr16(p, 0), py1 = (s16)(py0 + fr16(p, 2) - 1);
    return !(x0 > bx1 || x1 < bx0 || y1 < py0 || y0 > py1);
}

/* AP 0962:0cc6 masked_vec2(frame, x, y, flags) — replaces driver entry 29 (sprite) */
void masked_vec2(u16 fr, s16 x, s16 y, u16 flags)
{
    if (DSS(MASK_DEPTH) <= 0) { gfx_sprite_flip(fr, x, y, (s16)flags); return; }
    u16 s8 = DSW(0x26C8), sa = DSW(0x26CA);
    u8 s3 = DSB(0x26C3);
    s16 x0 = x, y0 = y, x1 = (s16)(x + DSS(fr + 6) - 1), y1 = (s16)(y + DSS(fr + 8) - 1);
    DSB(0x26C3) = 1;
    FarPtr a = ds_far(A_PTR);
    for (s16 i = 0; i < DSS(A_N); i++, a = far_add(a, 4)) {
        if (fr16(a, 2) < y0 || fr16(a, 0) > y1) continue;
        DSW(0x26C8) = (u16)fr16(a, 0);
        DSW(0x26CA) = (u16)fr16(a, 2);
        ((SprFn)codeptr_lookup(ds_far(SV_SPR)))(fr, x, y, flags);
    }
    DSW(0x26C8) = s8;
    DSW(0x26CA) = sa;
    DSB(0x26C3) = s3;
    for (s16 i = 0; i < DSS(B_N); i++) {
        if (!poly_hits(i, x0, y0, x1, y1)) continue;
        clip_hooks_install(far_at(ds_far(B_PTRS), i));
        gfx_sprite_scaled(fr, x, y, (s16)flags, DSS(fr + 6), DSS(fr + 8));
        clip_hooks_remove();
    }
}

/* AP 0962:0e47 masked_vec3(frame, x, y, angle, w, h, px, py) — replaces rot_fn DS:3436 */
void masked_vec3(u16 fr, s16 x, s16 y, u16 angle, s16 w, s16 h, s16 px, s16 py)
{
    if (DSS(MASK_DEPTH) <= 0) { gfx_sprite_rotated_ind(fr, x, y, angle, w, h, px, py); return; }
    u16 s8 = DSW(0x26C8), sa = DSW(0x26CA);
    u8 s3 = DSB(0x26C3);
    u16 m = stk_mark();
    u16 box = stk_alloc(8), lists = stk_alloc(0x28);
    rot_rect_setup(fr, (u16)x, (u16)y, angle, (u16)w, (u16)h, (u16)px, (u16)py, box);
    s16 x0 = DSS(box), y0 = DSS(box + 2), x1 = DSS(box + 4), y1 = DSS(box + 6);
    DSB(0x26C3) = 1;
    RotFn rot = (RotFn)codeptr_lookup(ds_far(SV_ROT));
    FarPtr a = ds_far(A_PTR);
    for (s16 i = 0; i < DSS(A_N); i++, a = far_add(a, 4)) {
        if (fr16(a, 2) < y0 || fr16(a, 0) > y1) continue;
        DSW(0x26C8) = (u16)fr16(a, 0);
        DSW(0x26CA) = (u16)fr16(a, 2);
        rot(fr, x, y, angle, w, h, px, py);
    }
    DSW(0x26C8) = s8;
    DSW(0x26CA) = sa;
    DSB(0x26C3) = s3;
    s16 k = 0;
    for (s16 i = 0; i < DSS(B_N); i++) {
        if (!poly_hits(i, x0, y0, x1, y1)) continue;
        FarPtr p = far_at(ds_far(B_PTRS), i);
        DSW((u16)(lists + 4 * k)) = p.off;
        DSW((u16)(lists + 4 * k + 2)) = p.seg;
        k++;
    }
    DSL((u16)(lists + 4 * k)) = 0;
    if (k) {
        clip_hooks_install_multi(ds_ptr(lists));
        rot(fr, x, y, angle, w, h, px, py);
        clip_hooks_remove();
    }
    stk_release(m);
}

/* AP 0962:1023 masked_vec4(frame, x, y, flags) — replaces scn_draw_fn DS:31E0 */
void masked_vec4(u16 fr, s16 x, s16 y, s16 flags)
{
    ScnFn scn = (ScnFn)codeptr_lookup(ds_far(SV_SCN));
    if (DSS(MASK_DEPTH) <= 0) { scn(fr, x, y, flags); return; }
    u16 s8 = DSW(0x26C8), sa = DSW(0x26CA);
    u8 s3 = DSB(0x26C3);
    s16 x0 = x, y0 = y, x1 = (s16)(x + DSS(fr + 6) - 1), y1 = (s16)(y + DSS(fr + 8) - 1);
    DSB(0x26C3) = 1;
    FarPtr a = ds_far(A_PTR);
    for (s16 i = 0; i < DSS(A_N); i++, a = far_add(a, 4)) {
        if (fr16(a, 2) < y0 || fr16(a, 0) > y1) continue;
        DSW(0x26C8) = (u16)fr16(a, 0);
        DSW(0x26CA) = (u16)fr16(a, 2);
        scn(fr, x, y, flags);
    }
    DSW(0x26C8) = s8;
    DSW(0x26CA) = sa;
    DSB(0x26C3) = s3;
    for (s16 i = 0; i < DSS(B_N); i++) {
        if (!poly_hits(i, x0, y0, x1, y1)) continue;
        clip_hooks_install(far_at(ds_far(B_PTRS), i));
        scn(fr, x, y, flags);
        clip_hooks_remove();
    }
}

__attribute__((constructor)) static void mask_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x0962079Au, (CodeFn)span_through_windows);
    codeptr_register(PROG_AP, 0x09620819u, (CodeFn)pixel_rows);
    codeptr_register(PROG_AP, 0x096208DEu, (CodeFn)masked_span_fill);
    codeptr_register(PROG_AP, 0x09620CC6u, (CodeFn)masked_vec2);
    codeptr_register(PROG_AP, 0x09620E47u, (CodeFn)masked_vec3);
    codeptr_register(PROG_AP, 0x09621023u, (CodeFn)masked_vec4);
}
