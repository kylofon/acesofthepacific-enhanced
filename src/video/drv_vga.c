/* The VGA320 driver (OVL:VGA: of VMCODE.OVL / VMCODE2.OVL, 9424 bytes, identical in both files) in C:
 * the 50 jump-table entries of video.md §3 on the VGA model of platform/vga.h.
 *
 * The driver is not loaded (video.md §6): gfx_init (video/gfx_init.c) allocates a DOS block for it like
 * the original, fills the jump table with far pointers <block>:<entry offset> and registers these C
 * functions for them (codeptr_register_abs). The driver's own code-segment variables live in that
 * block at their original offsets (cs:1FCA cache offset, cs:116D span-copy displacement, cs:00D6/00D8
 * its DS and base), its data segment is the caller's DGROUP + base/16 = the drv[] block.
 *
 * Planar model: every page segment S addresses planar bytes; pixel plane p of planar byte o is
 * vram[((S - A000) * 16 + o) * 4 + p] (vid_priv.h vput/vget). The entries are transcribed at that level
 * (planar offset + plane mask, 16-bit wrap-around, the same loop counts including the degenerate
 * ones), which reproduces the plane-dependent quirks (whole 4-pixel columns in latch copies, the
 * cache layout, the w & ~3 columns and the 4-aligned source of the sprite entries, dither phases).
 *
 * "C" entries take the words of their far cdecl frame; "R" entries model their register inputs. */
#include "video/vid_priv.h"

#include <string.h>

/* ------------------------------------------------------------------------------------ helpers */

static inline u8 lin_rd(u32 a) { return a < MEM_SIZE ? mem[a] : 0; }
static inline void lin_wr(u32 a, u8 v) { if (a < MEM_SIZE) mem[a] = v; }

static u16 drv_cs(void) { return DSW((u16)(lds(G_gfx_driver_ptr) + 2)); }   /* the driver block (DS:1A52) */
#define CSW(o) (*(u16 *)mp(drv_cs(), (u16)(o)))

/* Plane-masked write of one planar byte: planes 0/2 (even x) get ve, planes 1/3 (odd x) get vo. */
static void pwrite(u16 seg, u16 ofs, u8 mask, u8 ve, u8 vo)
{
    for (int p = 0; p < 4; p++)
        if (mask & (1 << p)) vput(seg, ofs, p, (p & 1) ? vo : ve);
}

/* Latch copy (write mode 1) of one planar byte, planes in mask. */
static void latch_byte(u16 dseg, u16 dofs, u16 sseg, u16 sofs, u8 mask)
{
    u8 latch[4];
    for (int p = 0; p < 4; p++) latch[p] = vget(sseg, sofs, p);
    for (int p = 0; p < 4; p++)
        if (mask & (1 << p)) vput(dseg, dofs, p, latch[p]);
}

static const u8 mask_left[4] = { 0x0F, 0x0E, 0x0C, 0x08 };    /* cs:01F5 / 091E */
static const u8 mask_single[4] = { 0x0F, 0x01, 0x03, 0x07 };  /* cs:01F9 / 0922 / 116F */
static const u8 mask_right[4] = { 0x00, 0x01, 0x03, 0x07 };   /* cs:01FD / 0926 */

/* The span writer shared by entries 10, 11 and 27 (02B1 / 0205 / 0932): x .. x+cx-1 on the row at
 * planar offset di; ve = value of even x (planes 0, 2), vo = odd x. cx is not checked here. */
static void span_write(u16 seg, u16 di, u16 x, u16 cx, u8 ve, u8 vo)
{
    u16 last = (u16)(x + cx - 1);
    if (((last ^ x) & 0x0FFC) == 0) {                           /* inside one planar byte */
        u8 m = (u8)((0xFF << (x & 3)) & mask_single[(u16)(x + cx) & 3]);
        pwrite(seg, (u16)(di + (x >> 2)), m, ve, vo);
        return;
    }
    di = (u16)(di + (x >> 2));
    u16 c = (u16)(cx - 4 + (x & 3));
    pwrite(seg, di++, mask_left[x & 3], ve, vo);
    for (u16 n = (u16)(c >> 2); n; n--) pwrite(seg, di++, 0x0F, ve, vo);
    pwrite(seg, di, mask_right[c & 3], ve, vo);
}

/* ------------------------------------------------------------------------ entry 0/49: init */

/* 0000 — video.md §2.3. base = INTRO 0A80 / PS 2300 / AP 26C0; callbacks = 19 far pointers (unused). */
FarPtr drv_init(u16 base, FarPtr callbacks)
{
    u16 cs = drv_cs();
    memcpy(mp(cs, 0x01A2), far_mp(callbacks), 38 * 2);          /* callback table (unused by VGA) */
    wr16(cs, 0x00D8, base);
    wr16(cs, 0x00D6, (u16)(cur_dgroup + (base >> 4)));
    DRVB(D_EGA_MODE) = 0xFF;
    DRVB(D_INITS) += 1;
    if (DRVB(D_HW_PRESENT) != 0) {
        vga_set_mode(0x13);                                     /* INT 10h AX=0013h */
        vga_set_unchained();                                    /* SC4/GC6/CRTC14/17, clears 256 KB, GC5/1/8 */
    }
    DRVW(D_PAGE_FRONT) = 0xA000;
    DRVW(D_PAGE_BACK) = 0xA400;
    DRVW(D_PAGE_BG) = 0xA800;
    for (u16 y = 0; y < 200; y++) DRVW(D_ROW_OFS + 2 * y) = (u16)(y * 80);
    DRVB(D_ADAPTER) = 8;
    return far_make(cs, 0x00DA);                                 /* DX:SI = the jump table */
}

/* ------------------------------------------------------------------------ entry 1: glyph */

/* 0E72 (R): BP = y, DX = x, CX = rows, ES:SI = glyph (1 byte per row, bit 7 leftmost). */
void drv_glyph1(s16 bp_y, s16 dx_x, u16 cx_rows, FarPtr es_si_glyph)
{
    u16 seg = DRVW(D_PAGE_WRITE);
    u16 di = (u16)(drv_row(bp_y) + ((u16)dx_x >> 2));
    int sh = dx_x & 3;
    u16 rows = cx_rows;
    if (DRVB(D_TEXT_FLAGS) == 0) {                              /* background box, 8 wide */
        u8 bg = DRVB(D_TEXT_BG);
        u16 d = di, r = rows;
        do {
            u16 bits = (u16)(0xFF00 >> sh);                     /* the cell's 8 columns in a 12-bit window */
            for (int b = 0; b < 3; b++) {
                u8 m = 0;
                for (int p = 0; p < 4; p++) if (bits & (0x8000 >> (b * 4 + p))) m |= (u8)(1 << p);
                pwrite(seg, (u16)(d + b), m, bg, bg);
            }
            d = (u16)(d + 80);
        } while (--r);
    }
    u8 fg = DRVB(D_TEXT_FG);
    u8 *g = far_mp(es_si_glyph);
    u16 d = di, r = rows;
    do {
        u16 bits = (u16)((*g++ << 8) >> sh);
        for (int b = 0; b < 3; b++) {
            u8 m = 0;
            for (int p = 0; p < 4; p++) if (bits & (0x8000 >> (b * 4 + p))) m |= (u8)(1 << p);
            pwrite(seg, (u16)(d + b), m, fg, fg);
        }
        d = (u16)(d + 80);
    } while (--r);
}

/* ------------------------------------------------------------------------ entry 2: line */

static void line_plot(u16 seg, s16 x, s16 y, u8 c) { vpx_put(seg, drv_row(y), x, c); }

/* 0753 (R): BX = x1, CX = y1, DX = x2, SI = y2 with x1 <= x2; colour drv[0E]; no clipping (§3.3). */
void drv_line(s16 bx_x1, s16 cx_y1, s16 dx_x2, s16 si_y2)
{
    u16 seg = DRVW(D_PAGE_WRITE);
    u8 c = DRVB(D_DRAW_COL);
    s16 x1 = bx_x1, y1 = cx_y1, x2 = dx_x2, y2 = si_y2;
    if (x1 == x2) {
        s16 a = y1 < y2 ? y1 : y2, b = y1 < y2 ? y2 : y1;
        for (s16 y = a;; y++) { line_plot(seg, x1, y, c); if (y == b) break; }
        return;
    }
    if (y1 == y2) {
        span_write(seg, drv_row(y1), (u16)x1, (u16)(x2 - x1 + 1), c, c);
        return;
    }
    s16 dx = (s16)(x2 - x1), dy = (s16)(y2 > y1 ? y2 - y1 : y1 - y2), ys = y2 > y1 ? 1 : -1;
    if (dy == dx) {
        for (s16 i = 0; i <= dx; i++) line_plot(seg, (s16)(x1 + i), (s16)(y1 + ys * i), c);
    } else if (dy < dx) {                                        /* x-major, from (x1, y1) */
        s16 e = (s16)(2 * dy - dx), x = x1, y = y1;
        line_plot(seg, x, y, c);
        for (s16 n = dx; n; n--) {
            x++;
            if (e < 0) e = (s16)(e + 2 * dy);
            else { y = (s16)(y + ys); e = (s16)(e + 2 * dy - 2 * dx); }
            line_plot(seg, x, y, c);
        }
    } else {                                                     /* y-major, always walking down */
        s16 x, y, xs;
        if (ys > 0) { x = x1; y = y1; xs = 1; } else { x = x2; y = y2; xs = -1; }
        s16 e = (s16)(2 * dx - dy);
        line_plot(seg, x, y, c);
        for (s16 n = dy; n; n--) {
            y++;
            if (e < 0) e = (s16)(e + 2 * dx);
            else { x = (s16)(x + xs); e = (s16)(e + 2 * dx - 2 * dy); }
            line_plot(seg, x, y, c);
        }
    }
}

/* ------------------------------------------------------------ entries 29 / 3 / 33: sprites */

/* 1412 (29) and 2128 (33) — video.md §3.4/3.5, transcribed from the planar code: the frame is drawn in
 * four plane passes; pass p covers destination columns p, p+4, ... of the visible width and reads the
 * source from ((S + frame.off) & ~3) + phase with a stride of 4, where S is the unaligned start index
 * and phase = S & 3 (computed before the frame offset is added: a frame whose pixel offset is not a
 * multiple of 4 is drawn shifted, as in the original). flags: 1 = upside down, 2 = mirrored.
 * opaque = entry 33 (zeros written, top clip starts one row early, flags 0). */
static void sprite_planar(u16 frame_ds, s16 x, s16 y, u16 flags, bool opaque)
{
    u16 fseg = DSW(frame_ds), foff = DSW(frame_ds + 2), w = DSW(frame_ds + 6), h = DSW(frame_ds + 8);
    u16 q = w >> 2;                                              /* [bp-4] */
    u16 w4h = (u16)((u16)(q << 2) * h);                          /* cs:13FC (16-bit mul) */
    u16 si = 0;
    if (!opaque) {
        if (flags & 2) si = (u16)(w - 1);
        if (flags & 1) si = (u16)(si + w4h - w);
    }
    u16 di = (u16)((u16)(drv_row(y) << 2) + (u16)x);             /* linear pixel address */
    u16 vis = (u16)(q << 2);                                     /* dx: visible width */
    u16 rows = h;                                                /* cs:1404 */
    u16 skip = 0;                                                /* bx: columns skipped per row */
    if (DRVB(D_CLIP_ON)) {
        s16 a = (s16)(DRVS(D_CLIP_X2) - x - (s16)vis);             /* right */
        if (a < 0) {
            a = (s16)(-a - 1);
            skip = (u16)a;
            vis = (u16)(vis - a);
            if ((u16)((u16)a >> 2) >= q) return;
        }
        a = (s16)(DRVS(D_CLIP_Y2) - y - (s16)rows);                /* bottom */
        if (a < 0) {
            a = (s16)(-a - 1);
            u16 r = (u16)(rows - (u16)a);
            bool borrow = (u16)a > rows;
            rows = r;
            if (borrow || rows == 0) return;
        }
        a = (s16)(DRVS(D_CLIP_Y1) - y);                            /* top */
        if (a > 0) {
            rows = (u16)(rows - a);
            u16 k = (u16)(opaque ? a - 1 : a);
            u16 d = (u16)((u16)(k * q) << 2);
            if (!opaque && (flags & 1)) d = (u16)-d;
            si = (u16)(si + d);
            di = (u16)((u16)(drv_row(DRVS(D_CLIP_Y1)) << 2) + (u16)x);
            if (rows == 0 || (s16)rows < 0) return;
        }
        a = (s16)(DRVS(D_CLIP_X1) - x);                            /* left */
        if (a > 0) {
            di = (u16)(di + a);
            skip = (u16)(skip + a);
            if (!opaque && (flags & 2)) si = (u16)(si - 2 * a);
            vis = (u16)(vis - a);
            si = (u16)(si + a);
            if ((u16)((u16)a >> 2) >= q) return;
        }
    }
    u16 plane = di & 3;                                          /* cs:1402 */
    u16 phase = (u16)(((!opaque && (flags & 2)) ? (u16)~si : si) & 3);   /* cs:1400 */
    di >>= 2;
    if (!opaque && (flags & 2)) phase ^= 3;
    u16 base = (u16)((si + foff) & 0xFFFC);                      /* cs:13FE */
    u16 rowdi = di;                                              /* cs:1408 */
    u16 wseg = DRVW(D_PAGE_WRITE);
    int variant = opaque ? 0 : (flags & 3);
    bool back = variant >= 2;                                    /* mirrored: source read backwards */

    /* first pass source: base + phase, phase advances +1 (normal) / -1 (mirrored) */
    u16 src = (u16)(base + phase);
    phase = (u16)((back ? phase - 1 : phase + 1) & 3);
    for (int p = 0; p < 4; p++) {
        /* plane selection (15c2): plane = cs:1402, wrapping to the next planar byte */
        int pl = plane;
        plane++;
        if (pl == 4) { rowdi++; pl = 0; plane = 1; }
        u16 d = rowdi;
        u16 cnt = (u16)((u16)(vis + 3 - p) >> 2);
        u16 bx = (u16)((skip + p) & 0xFFFC);
        u16 rowadv;
        switch (variant) {
        case 0: rowadv = bx; break;                              /* si += bx */
        case 1: rowadv = (u16)(bx - 2 * w); break;               /* bx - 2w */
        case 2: rowadv = (u16)(2 * w - bx); break;               /* 2w - bx */
        default: rowadv = (u16)-bx; break;                       /* si -= bx */
        }
        if (cnt) {
            u16 s = src, r = rows;
            do {
                for (u16 c = cnt; c; c--) {
                    u8 v = rd8(fseg, s);
                    s = (u16)(back ? s - 4 : s + 4);
                    if (v || opaque) vput(wseg, d, pl, v);
                    d++;
                }
                s = (u16)(s + rowadv);
                d = (u16)(d + 80 - cnt);
            } while (--r);
        }
        if (p == 3) break;
        /* next pass source (1697 / 1b3a) */
        if (!back) { if (phase == 0) base = (u16)(base + 4); }
        else { if (phase == 3) base = (u16)(base - 4); }
        src = (u16)(base + phase);
        phase = (u16)((back ? phase - 1 : phase + 1) & 3);
    }
}

/* 1412 (C): frame record {+0 seg, +2 off, +4 kind, +6 w, +8 h}; flags 1 = upside down, 2 = mirror. */
void drv_sprite_flip(u16 frame_ds, s16 x, s16 y, u16 flags) { sprite_planar(frame_ds, x, y, flags, false); }
/* 0E58 (C) = entry 29 with flags 0. */
void drv_sprite(u16 frame_ds, s16 x, s16 y) { sprite_planar(frame_ds, x, y, 0, false); }
/* 2128 (C): opaque, flags 0, top clip starts at row k-1. */
void drv_sprite_opaque(u16 frame_ds, s16 x, s16 y) { sprite_planar(frame_ds, x, y, 0, true); }

/* ------------------------------------------------------------------ entries 4-8: rectangles */

/* 1103 (C): latch copy read page -> write page, whole planar bytes; rows h (0 = 65536). */
void drv_copy_rect(s16 x, s16 y, s16 w, s16 h)
{
    u16 n = (u16)((u16)((x & 3) + w + 3) >> 2);
    u16 o = (u16)(drv_row(y) + ((u16)x >> 2));
    u16 rseg = DRVW(D_PAGE_READ), wseg = DRVW(D_PAGE_WRITE);
    u16 r = (u16)h;
    do {
        for (u16 i = 0; i < n; i++) latch_byte(wseg, (u16)(o + i), rseg, (u16)(o + i), 0x0F);
        o = (u16)(o + 80);
    } while (--r);
}

/* 0F67 (C): dst[j*w+i] = Pread[y+j][x+i] (w, h: 0 = 65536). */
void drv_get_rect(FarPtr dst, s16 x, s16 y, s16 w, s16 h)
{
    u16 row = (u16)(drv_row(y) + ((u16)x >> 2));
    u16 rseg = DRVW(D_PAGE_READ);
    u16 di = dst.off, r = (u16)h;
    do {
        u16 si = row;
        int pl = x & 3;
        u16 c = (u16)w;
        do {
            wr8(dst.seg, di++, vget(rseg, si, pl));
            if (++pl == 4) { pl = 0; si++; }
        } while (--c);
        row = (u16)(row + 80);
    } while (--r);
}

/* 0FCA (C): DX:AX = w * h (unsigned 16 x 16). */
u32 drv_rect_size(u16 w, u16 h) { return (u32)w * h; }

/* 0FD6 (C): Pwrite[y+j][x+i] = src[j*w+i], opaque (w, h: 0 = 65536). */
void drv_put_rect(FarPtr src, s16 x, s16 y, s16 w, s16 h)
{
    u16 row = (u16)(drv_row(y) + ((u16)x >> 2));
    u16 wseg = DRVW(D_PAGE_WRITE);
    u16 si = src.off, r = (u16)h;
    do {
        u16 di = row;
        int pl = x & 3;
        u16 c = (u16)w;
        do {
            vput(wseg, di, pl, rd8(src.seg, si++));
            if (++pl == 4) { pl = 0; di++; }
        } while (--c);
        row = (u16)(row + 80);
    } while (--r);
}

/* 10AD (C): swap back/front, CRTC start = old back page, optional wait for the next retrace (§3.8). */
void drv_flip(u16 wait)
{
    u16 old_back = DRVW(D_PAGE_BACK);
    DRVW(D_PAGE_BACK) = DRVW(D_PAGE_FRONT);
    DRVW(D_PAGE_FRONT) = old_back;
    u16 port = rd16(0x40, 0x63);
    u16 start = (u16)(old_back << 4);
    vga_outw(port, (u16)(0x0C | (start & 0xFF00)));
    vga_outw(port, (u16)(0x0D | (start << 8)));
    if (wait) vga_wait_vretrace_start();                         /* while (in 3DAh & 8); while !(...) */
}

/* 01EE / 064C / 064D / 24CF: retf (entries 9, 12, 16, 25, 26, 28, 39-48). */
void drv_nop(void) { }

/* ----------------------------------------------------------------- entries 10/11: spans */

/* 02B1 (R): ES = page, DI = row, BX = x, CX = count, AL/AH colours, DX parity (callers pass y). */
void drv_hspan(u16 es_page, u16 di_row, s16 bx_x, s16 cx_count, u8 al_a, u8 ah_b, u16 dx_parity)
{
    if (cx_count <= 0) return;
    if (ah_b != 0) { drv_hspan_dither(es_page, di_row, bx_x, cx_count, al_a, ah_b, dx_parity); return; }
    span_write(es_page, di_row, (u16)bx_x, (u16)cx_count, al_a, al_a);
}

/* 0205 (R): dither; if DX odd swap AL/AH; even x gets AH, odd x AL. No count check. */
void drv_hspan_dither(u16 es_page, u16 di_row, s16 bx_x, s16 cx_count, u8 al_a, u8 ah_b, u16 dx_parity)
{
    if (dx_parity & 1) { u8 t = al_a; al_a = ah_b; ah_b = t; }
    span_write(es_page, di_row, (u16)bx_x, (u16)cx_count, ah_b, al_a);
}

/* -------------------------------------------------------- entries 13/14/15: bitmap preparation */

/* 0CD6 (C): sum of w*h over the zero-terminated near list; *out = 0. */
u32 drv_bm_size(u16 list_ds, u16 out_ds)
{
    u32 sum = 0;
    for (u16 p = list_ds; DSW(p); p = (u16)(p + 2)) {
        u16 f = DSW(p);
        sum += (u32)DSW(f + 6) * DSW(f + 8);
    }
    DSW(out_ds) = 0;
    return sum;
}

/* 0D09 (C): assign consecutive normalised pointers inside buf, then expand n packed bytes in place from
 * the end: buf[2k] = b >> 4, buf[2k+1] = b & 0Fh (huge pointers). PORT: linear arithmetic; the
 * driver's destination set-up adjusts the segment by -1000h when the low word of n is 0 (not reached). */
void drv_bm_layout(u16 list_ds, FarPtr buf, u32 n)
{
    u16 es = buf.seg, di = buf.off;
    for (u16 p = list_ds; DSW(p); p = (u16)(p + 2)) {
        u16 f = DSW(p);
        DSW(f) = (u16)(es + (di >> 4));
        DSW(f + 2) = di & 15;
        u16 sz = (u16)(DSW(f + 6) * DSW(f + 8));
        u32 t = (u32)di + sz;
        di = (u16)t;
        if (t >> 16) es = (u16)(es + 0x1000);
    }
    u32 base = far_lin(buf);
    u32 nstart = (n & 0xFFFF) | ((u32)(n >> 16 & 0xFF) << 16);  /* start: low word + low byte of the high word */
    u32 src = base + nstart - 1, dst = base + 2 * (nstart - 1);
    u32 cnt = n;                                                 /* PORT: n = 0 (4G iterations) not reached */
    while (cnt) {
        u8 b = lin_rd(src);
        lin_wr(dst, b >> 4);
        lin_wr(dst + 1, b & 0x0F);
        src--; dst -= 2; cnt--;
    }
}

/* 0E04 (C): dst[2k] |= src[k] & F0h, dst[2k+1] |= (src[k] & 0Fh) << 4, k < n (0 = 65536). */
void drv_bm_merge_hi(FarPtr src, FarPtr dst, u16 n)
{
    u32 s = far_lin(src), d = far_lin(dst);
    u16 c = n;
    do {
        u8 b = lin_rd(s++);
        lin_wr(d, lin_rd(d) | (b & 0xF0));
        lin_wr(d + 1, (u8)(lin_rd(d + 1) | (b & 0x0F) << 4));
        d += 2;
    } while (--c);
}

/* ------------------------------------------------------------- entries 17/18: nibble screens */

/* 126D (C): far source (normalised), xb = planar column, y, w pixels (w/2 bytes per row), rows (byte
 * counters: 0 = 256). Byte b0 -> pixels 0 (high nibble), 1 (low); b1 -> pixels 2, 3. */
void drv_nib_put(FarPtr src, u16 xb, u16 y, u16 w, u16 rows)
{
    u16 ds = (u16)(src.seg + (src.off >> 4)), si = src.off & 15;
    u16 wseg = DRVW(D_PAGE_WRITE);
    u8 groups = (u8)((w >> 1) >> 1);
    u8 r = (u8)rows;
    do {
        u16 di = (u16)(drv_row((s16)y) + xb);
        u8 g = groups;
        do {
            u8 b0 = rd8(ds, si++), b1 = rd8(ds, si++);
            vput(wseg, di, 0, b0 >> 4);
            vput(wseg, di, 1, b0 & 15);
            vput(wseg, di, 2, b1 >> 4);
            vput(wseg, di, 3, b1 & 15);
            di++;
        } while (--g);
        y++;
    } while (--r);
}

/* 131C (C): as 17 but P |= nibble << 4 (reads the write page). */
void drv_nib_or_hi(FarPtr src, u16 xb, u16 y, u16 w, u16 rows)
{
    u16 ds = (u16)(src.seg + (src.off >> 4)), si = src.off & 15;
    u16 wseg = DRVW(D_PAGE_WRITE);
    u8 groups = (u8)((w >> 1) >> 1);
    u8 r = (u8)rows;
    do {
        u16 di = (u16)(drv_row((s16)y) + xb);
        u8 g = groups;
        do {
            u8 b0 = rd8(ds, si++), b1 = rd8(ds, si++);
            u8 *p;
            if ((p = vmem(wseg, di, 0))) { *p |= b0 & 0xF0; enh_tag(p); }         /* ENH: tags */
            if ((p = vmem(wseg, di, 1))) { *p |= (u8)(b0 << 4); enh_tag(p); }
            if ((p = vmem(wseg, di, 2))) { *p |= b1 & 0xF0; enh_tag(p); }
            if ((p = vmem(wseg, di, 3))) { *p |= (u8)(b1 << 4); enh_tag(p); }
            di++;
        } while (--g);
        y++;
    } while (--r);
}

/* ------------------------------------------------- entries 19/36/37: per-pixel source spans */

/* Destination of pixel k of a span starting at x on the row at planar offset di (the planar walk of
 * the driver: plane (x&3)+k, next byte on wrap). */
static inline void span_px(u16 seg, u16 di, u16 x, u16 k, u8 v)
{
    u16 t = (u16)((x & 3) + k);
    vput(seg, (u16)(di + (x >> 2) + (t >> 2)), t & 3, v);
}

/* 050E (R): as 37 with v translated by xlatb through the table at offset drv[6BC]; the zero test is on
 * the untranslated value. Correction to video.md §3.17: xlatb reads DS:[BX + AL] with DS = the source
 * segment (restored before the loop), so the table is at src.seg:drv[6BC], not in SS. No caller known. */
void drv_cols_xlat(u16 es_page, u16 di_row, s16 dx_x, s16 cx_count, FarPtr ds_si_src, u16 bp_table_ds)
{
    u16 xl = DRVW(D_XLAT_TAB);
    for (s16 k = 0; k < cx_count; k++) {
        u8 v = rd8(ds_si_src.seg, (u16)(ds_si_src.off + DSW(bp_table_ds + 2 * k)));
        if (v) span_px(es_page, di_row, (u16)dx_x, (u16)k, rd8(ds_si_src.seg, (u16)(xl + v)));
    }
}

/* 0334 (R, C-like frame): [bp+6] = near pointer to {seg, off} (source base), [bp+8] = extra offset,
 * [bp+0A] = near pointer to the word table T; DX = x (negative: the first -x pixels skipped), CX = count.
 * v = src.seg:(u16)(extra + src.off + T[k]); 0 transparent. SI (2 * row) is read only by AP's clip hook. */
void drv_span_tex(u16 base_pair_ds, u16 extra, u16 table_ds, s16 dx_x, s16 cx_count, u16 es_page, u16 di_row, u16 si_row2)
{
    (void)si_row2;
    s16 x = dx_x, cnt = cx_count;
    if (x < 0) {
        cnt = (s16)(cnt + x);
        if (cnt <= 0) return;
        table_ds = (u16)(table_ds - 2 * x);
        x = 0;
    }
    u16 seg = DSW(base_pair_ds);
    u16 bx = (u16)(extra + DSW(base_pair_ds + 2));
    /* plane passes: pass p covers pixels p, p+4, ... of count (cx + 3 - p) >> 2 (0 = 65536 is not
     * reachable: count >= 1 here) */
    for (s16 k = 0; k < cnt; k++) {
        u8 v = rd8(seg, (u16)(bx + DSW(table_ds + 2 * k)));
        if (v) span_px(es_page, di_row, (u16)x, (u16)k, v);
    }
}

/* 0438 (R): DS:SI = source row, SS:BP = word table T; v = DS:[SI + T[k]], 0 transparent. BX (row) is
 * read only by AP's clip hook. */
void drv_span_cols(u16 es_page, u16 di_row, s16 dx_x, s16 cx_count, FarPtr ds_si_src, u16 bp_table_ds, u16 bx_row)
{
    (void)bx_row;
    for (s16 k = 0; k < cx_count; k++) {
        u8 v = rd8(ds_si_src.seg, (u16)(ds_si_src.off + DSW(bp_table_ds + 2 * k)));
        if (v) span_px(es_page, di_row, (u16)dx_x, (u16)k, v);
    }
}

/* ------------------------------------------------------------------ entry 20/34/35: palette */

/* The DAC upload of entry 35 from any segment (entries 20 and 34 call it with DS = palette segment). */
static void dac_upload_far(FarPtr src, u16 first, u16 count)
{
    vga_wait_vretrace_active();                                  /* while !(in 3DAh & 8) */
    /* PORT: the dummy 3C9h write when (in 3C7h & 3) != 3 is dropped */
    vga_out(0x3C8, (u8)first);
    u8 *p = far_mp(src);
    for (u32 i = 0; i < (u32)count * 3; i++) vga_out(0x3C9, p[i]);
}

/* 0BC2 (C): count*3 bytes from DS:src to the DAC from index first, after waiting for vertical retrace. */
void drv_dac_upload(u16 src_ds, u16 first, u16 count) { dac_upload_far(ds_ptr(src_ds), first, count); }

/* 0C16 (C): if seg(pal): DAC 0..255 = pal; copy the 768 bytes to pal_buf+0 and pal_buf+300h. */
void drv_set_palette(FarPtr pal)
{
    if (pal.seg == 0) return;
    dac_upload_far(pal, 0, 256);
    u16 bseg = DRVW(D_PAL_BUF + 2);                              /* pal_buf offset assumed 0 */
    memmove(mp(bseg, 0), far_mp(pal), 768);
    memmove(mp(bseg, 0x300), far_mp(pal), 768);
}

/* 0C58 (C): cur[i][k] = tgt[i][k]*l/63 + tgt[c][k]*(63-l)/63 (8-bit mul/div, each term truncated), then
 * upload cur[first .. first+count-1]. */
void drv_fade_step(u16 first, u16 count, u16 c, u16 l)
{
    u16 bseg = DRVW(D_PAL_BUF + 2);
    u8 lb = (u8)l, ib = (u8)(63 - lb);
    for (u16 i = 0; i < count; i++) {
        for (int k = 0; k < 3; k++) {
            u16 o = (u16)((first + i) * 3 + k);
            u8 t1 = rd8(bseg, (u16)(0x300 + o)), t2 = rd8(bseg, (u16)(0x300 + c * 3 + k));
            u8 a = div8_retry((u16)(t1 * lb), 63, NULL);
            u8 b = div8_retry((u16)(t2 * ib), 63, NULL);
            wr8(bseg, o, (u8)(a + b));
        }
    }
    dac_upload_far(far_make(bseg, (u16)(first * 3)), first, count);
}

/* ------------------------------------------------------------------- entries 21/22: pixels */

/* 103F (C): AX = Pread[y][x]. */
u16 drv_get_pixel(s16 x, s16 y) { return vget(DRVW(D_PAGE_READ), (u16)(drv_row(y) + ((u16)x >> 2)), x & 3); }
/* 1075 (C): Pwrite[y][x] = c. */
void drv_put_pixel(s16 x, s16 y, u16 c) { vput(DRVW(D_PAGE_WRITE), (u16)(drv_row(y) + ((u16)x >> 2)), x & 3, (u8)c); }

/* --------------------------------------------------------------- entries 23/24: span copy */

/* 1173 (R): ES:SI = span list {y0, n, n x (xl, xr)}; latch copy of xl..xr from the read page at planar
 * displacement cs:116D to the write page (edge bytes plane-masked). n = 0 -> 65536 rows. */
void drv_span_copy(FarPtr es_si_spans)
{
    u16 wseg = DRVW(D_PAGE_WRITE), rseg = DRVW(D_PAGE_READ);
    u16 disp = CSW(0x116D);
    u16 ls = es_si_spans.seg, lp = es_si_spans.off;
    u16 di = drv_row((s16)rd16(ls, lp));
    u16 n = rd16(ls, (u16)(lp + 2));
    lp = (u16)(lp + 4);
    do {
        u16 xl = rd16(ls, lp), xr = rd16(ls, (u16)(lp + 2));
        lp = (u16)(lp + 4);
        if (((xl ^ xr) & 0x0FFC) == 0) {
            u16 d = (u16)(di + (xl >> 2));
            u8 m = (u8)((0x0F << (xl & 3)) & mask_single[(u16)(xr + 1) & 3]);
            latch_byte(wseg, d, rseg, (u16)(d + disp), m);
        } else {
            s16 bx = (s16)(xr - xl);
            if (bx > 0) {
                u16 d = (u16)(di + (xl >> 2));
                u16 cx = xl & 3;
                bx = (s16)(bx + (s16)cx - 3);
                latch_byte(wseg, d, rseg, (u16)(d + disp), (u8)(0x0F << cx));
                d++;
                for (u16 k = (u16)((u16)bx >> 2); k; k--, d++) latch_byte(wseg, d, rseg, (u16)(d + disp), 0x0F);
                u16 c = (u16)bx & 3;
                if (c) latch_byte(wseg, d, rseg, (u16)(d + disp), (u8)(0xF0 >> (8 - c) | 0xF0 << c));
            }
        }
        di = (u16)(di + 80);
    } while (--n);
}

/* 1252 (C): cs:116D = y*80 + (x >> 2) (signed shift). */
void drv_span_copy_ofs(s16 x, s16 y) { CSW(0x116D) = (u16)((u16)(y * 80) + (u16)(x >> 2)); }

/* --------------------------------------------------------------- entry 27: polygon spans */

/* 0932 (R): ES:SI = span list; colours drv[0D] (A) / drv[0F] (B); B != 0 = checkerboard with the phase
 * of (y0 + xl of the first span). The row table is read in DGROUP. The first row is always processed. */
void drv_poly_spans(FarPtr es_si_spans)
{
    u16 wseg = DRVW(D_PAGE_WRITE);
    u16 ls = es_si_spans.seg, lp = es_si_spans.off;
    u16 y0 = rd16(ls, lp);
    s16 n = (s16)rd16(ls, (u16)(lp + 2));
    lp = (u16)(lp + 4);
    u8 a = DRVB(D_FILL_COL), b = DRVB(D_FILL_COL2);
    u16 bp = (u16)(y0 << 1);
    bool odd_row = (((bp >> 1) + rd16(ls, lp)) & 1) != 0;        /* start pattern (0a16) */
    for (;;) {
        s16 xl = (s16)rd16(ls, lp), xr = (s16)rd16(ls, (u16)(lp + 2));
        lp = (u16)(lp + 4);
        s16 cnt = (s16)(xr - xl);
        if (cnt >= 0) {
            cnt++;
            u16 di = DSW((u16)(bp + D_ROW_OFS + drv_base()));
            if (b == 0) span_write(wseg, di, (u16)xl, (u16)cnt, a, a);
            else if (odd_row) span_write(wseg, di, (u16)xl, (u16)cnt, b, a);   /* 92C: even x = B */
            else span_write(wseg, di, (u16)xl, (u16)cnt, a, b);              /* 92E: even x = A */
        }
        if (--n <= 0) break;
        bp = (u16)(bp + 2);
        odd_row = !odd_row;
    }
}

/* ------------------------------------------------------------ entries 30/31/32: bitmap cache */

/* 1FCC (C): latch copy of h rows x w/4 planar bytes from the frame's cache position to the write page at
 * y*80 + (x >> 2); opaque, unclipped (h = 0 -> 65536 rows). */
void drv_cache_blit(u16 frame_ds, s16 x, s16 y)
{
    u16 di = (u16)(drv_row(y) + ((u16)x >> 2));
    u16 wseg = DRVW(D_PAGE_WRITE);
    u16 cseg = DSW(frame_ds), si = DSW(frame_ds + 2);
    u16 bx = DSW(frame_ds + 6) >> 2;
    u16 r = DSW(frame_ds + 8);
    do {
        for (u16 i = 0; i < bx; i++) latch_byte(wseg, di++, cseg, si++, 0x0F);
        di = (u16)(di + 80 - bx);
    } while (--r);
}

/* 2067 (C): copy each frame to AC00 at cs:1FCA planar (plane p = pixels p, p+4, ...), repoint the
 * frame there, then free the DOS block of the first frame's original segment. Returns the list. */
u16 drv_cache_upload(u16 list_ds)
{
    u16 first_seg = DSW(DSW(list_ds));
    for (u16 p = list_ds; DSW(p); p = (u16)(p + 2)) {
        u16 f = DSW(p);
        u16 di = CSW(0x1FCA);
        u16 si = DSW(f + 2);
        DSW(f + 2) = di;
        u16 n = (u16)((u16)(DSW(f + 6) * DSW(f + 8)) >> 2);
        CSW(0x1FCA) = (u16)(CSW(0x1FCA) + n);
        u16 ds = DSW(f);
        DSW(f) = 0xAC00;
        for (int pl = 0; pl < 4; pl++) {
            u16 s = (u16)(si + pl), d = di, c = n;
            do {
                vput(0xAC00, d++, pl, rd8(ds, s));
                s = (u16)(s + 4);
            } while (--c);
        }
    }
    dos_mem_free(first_seg);                                     /* INT 21h/49h */
    return list_ds;
}

/* 2113 (C): cs:1FCA = 0. */
void drv_cache_reset(void) { CSW(0x1FCA) = 0; }

/* -------------------------------------------------------------------- entry 38: row copy */

/* 064E (R): ES = page, DI = row, BX = x, CX = count (0 = 65536), DS:SI = source, CF = direction:
 * CF = 0: P[x+k] = src[k]; CF = 1: P[x-k] = src[k]. Opaque. DX (row) is read only by AP's clip hook. */
void drv_row_copy(u16 es_page, u16 di_row, s16 bx_x, s16 cx_count, FarPtr ds_si_src, bool cf_backwards, u16 dx_row)
{
    (void)dx_row;
    u16 x = (u16)bx_x;
    u16 di = (u16)(di_row + (x >> 2));
    int pl = x & 3;
    u16 bx = (u16)(cx_count + 3);
    u16 si = ds_si_src.off;
    for (int p = 0; p < 4; p++) {
        u16 c = bx >> 2;
        if (p > 0) {
            if (c == 0) break;
            if (!cf_backwards) { pl++; if (pl == 4) { pl = 0; di++; } }
            else { pl--; if (pl < 0) { pl = 3; di--; } }
        }
        u16 d = di, s = si;
        do {
            vput(es_page, d, pl, rd8(ds_si_src.seg, s));
            d = (u16)(cf_backwards ? d - 1 : d + 1);
            s = (u16)(s + 4);
        } while (--c);
        bx--;
        si++;
    }
}
