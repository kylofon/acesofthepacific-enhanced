/* Fonts and text (video_library.md §8): font slot selection and metrics, the fast path (driver entry 1,
 * 8-pixel cells) and the slow per-pixel path. INTRO 05da / PS 16e3 / AP 1d32.
 *
 * Slot fields: font_w/h/first/count in the drv[] block (drv[34+i] ...), font_kind, font_uline, font_glyphs,
 * font_ofs, font_wid through the layout (INTRO font_glyphs = DS:2ED4, named font_ptr there). */
#include "video/vid_priv.h"

static u16 f_kind(void) { return lds(G_font_kind); }
static u16 f_uline(void) { return lds(G_font_uline); }
static u16 f_ofs(void) { return lds(G_font_ofs); }
static u16 f_wid(void) { return lds(G_font_wid); }

/* INTRO 05da:30f8 / PS 16e3:370c / AP 1d32:5546: 0 < i < 20 and glyph pointer non-null */
s16 gfx_font_valid(s16 slot)
{
    if (slot <= 0 || slot >= 20) return 0;
    FarPtr g = ds_far((u16)(ds_font_glyphs() + 4 * slot));
    return (g.off | g.seg) ? 1 : 0;
}

/* INTRO 05da:2fc1 / PS 16e3:35d5 / AP 1d32:540f. slot 0: the slot (1..19) whose glyph pointer equals
 * the current font's (20 if none: the loop index is returned; 0 if no current font); else copy slot i
 * to slot 0 (all nine fields) if valid and return i, 0 if invalid. */
s16 gfx_font_select(s16 slot)
{
    u16 g = ds_font_glyphs();
    if (slot == 0) {
        FarPtr cur = ds_far(g);
        if (far_is_null(cur)) return 0;
        s16 i;
        for (i = 1; i < 20; i++) {
            FarPtr p = ds_far((u16)(g + 4 * i));
            if (p.seg == cur.seg && p.off == cur.off) break;
        }
        return i;
    }
    if (!gfx_font_valid(slot)) return 0;
    DSB(f_kind()) = DSB((u16)(f_kind() + slot));
    DRVB(D_FONT_W) = DRVB(D_FONT_W + slot);
    DRVB(D_FONT_H) = DRVB(D_FONT_H + slot);
    DSB(f_uline()) = DSB((u16)(f_uline() + slot));
    DRVB(D_FONT_FIRST) = DRVB(D_FONT_FIRST + slot);
    DRVB(D_FONT_COUNT) = DRVB(D_FONT_COUNT + slot);
    ds_far_wr(g, ds_far((u16)(g + 4 * slot)));
    ds_far_wr(f_ofs(), ds_far((u16)(f_ofs() + 4 * slot)));
    ds_far_wr(f_wid(), ds_far((u16)(f_wid() + 4 * slot)));
    return slot;
}

/* INTRO 05da:3098 / PS 16e3:36ac / AP 1d32:54e6 */
s16 gfx_font_width(s16 slot)
{
    if (gfx_font_valid(slot) || slot == 0) return DRVB((u16)(D_FONT_W + slot));
    return 0;
}

/* INTRO 05da:30c8 / PS 16e3:36dc / AP 1d32:5516 */
s16 gfx_font_height(s16 slot)
{
    if (gfx_font_valid(slot) || slot == 0) return DRVB((u16)(D_FONT_H + slot));
    return 0;
}

static bool font_prop(void) { FarPtr o = ds_far(f_ofs()); return (o.off | o.seg) != 0; }

/* INTRO 05da:3133 / PS 16e3:3747 / AP 1d32:5581: sum of the widths of the characters inside the font */
s16 gfx_text_width_far(FarPtr s)
{
    u16 sum = 0;
    bool prop = font_prop();
    FarPtr wid = ds_far(f_wid());
    while (rd8(s.seg, s.off) != 0) {
        s16 i = (s16)(rd8(s.seg, s.off) - DRVB(D_FONT_FIRST));
        s.off++;
        if (i < 0 || DRVB(D_FONT_COUNT) <= i) continue;
        sum = (u16)(sum + (prop ? rd8(wid.seg, (u16)(wid.off + i)) : DRVB(D_FONT_W)));
    }
    return (s16)sum;
}

/* INTRO 05da:3122 / PS 16e3:3736 / AP 1d32:5570 */
s16 gfx_text_width(u16 s_ds) { return gfx_text_width_far(ds_ptr(s_ds)); }

/* INTRO 05da:3573 / PS 16e3:3b87 / AP 1d32:59c1: 0 if c is outside the current font, else 1 and the
 * width/height through the (optional) near pointers. */
s16 gfx_char_metrics(u8 c, u16 w_ds, u16 h_ds)
{
    s16 i = (s16)(c - DRVB(D_FONT_FIRST));
    if (i < 0 || DRVB(D_FONT_COUNT) <= i) return 0;
    u16 w;
    if (font_prop()) { FarPtr wid = ds_far(f_wid()); w = rd8(wid.seg, (u16)(wid.off + i)); }
    else w = DRVB(D_FONT_W);
    u16 h = DRVB(D_FONT_H);
    if (w_ds) DSW(w_ds) = w;
    if (h_ds) DSW(h_ds) = h;
    return 1;
}

/* INTRO 05da:3193 / PS 16e3:37a7 / AP 1d32:55e1 (near) — video_library.md §8.4: returns the advance. */
static u16 gfx_char_slow(u8 c, s16 x, s16 y)
{
    u8 save = DRVB(D_TEXT_FG);
    s16 i = (s16)(c - DRVB(D_FONT_FIRST));
    if (i < 0 || DRVB(D_FONT_COUNT) <= i) return 0;
    u8 kind = DSB(f_kind());
    u16 w, h;
    FarPtr g = ds_far(ds_font_glyphs());
    if (kind & 1) {
        FarPtr wid = ds_far(f_wid()), ofs = ds_far(f_ofs());
        w = rd8(wid.seg, (u16)(wid.off + i));
        h = DRVB(D_FONT_H);
        g.off = (u16)(g.off + rd16(ofs.seg, (u16)(ofs.off + 2 * i)));
    } else if (kind == 2) {
        w = DRVB(D_FONT_W);
        h = DRVB(D_FONT_H);
        g.off = (u16)(g.off + (u16)((u16)(i * w) * h));
    } else {
        w = DRVB(D_FONT_W);
        h = DRVB(D_FONT_H);
        g.off = (u16)(g.off + (u16)((u16)(((w + 7) >> 3) * i) * h));
    }
    bool direct = x >= DRVS(D_CLIP_X1) && y >= DRVS(D_CLIP_Y1) && (u16)(x + w) <= DRVW(D_CLIP_X2) &&
                  (u16)(y + h) <= DRVW(D_CLIP_Y2);
    DrvPutPixelFn e22 = (DrvPutPixelFn)drv_entry(22);
#define PUT(px, py, col) (direct ? e22((px), (py), (u16)(col)) : gfx_put_pixel((px), (py), (s16)(col)))
    bool onebpp = kind <= 1;
    u8 flags;
    if (DRVB(D_TEXT_FLAGS) & 4) x = (s16)(x + (h >> 1));
    for (u16 r = 0; r < h; r++) {
        flags = DRVB(D_TEXT_FLAGS);
        if (!(flags & 1)) {
            DRVB(D_DRAW_COL) = DRVB(D_TEXT_BG);
            gfx_line(x, y, (s16)(x + w), y);
        }
        u8 mask = 0x80, pix;
        for (u16 cc = 0; cc < w; cc++) {
            if (onebpp) {
                if (mask == 0) { mask = 0x80; g.off++; }
                pix = rd8(g.seg, g.off) & mask;
                mask >>= 1;
            } else {
                pix = rd8(g.seg, g.off);
                if (pix) DRVB(D_TEXT_FG) = pix < 5 ? DSB((u16)(lds(G_font_aa_map) + pix)) : pix;
                if ((u16)(w - 1) > cc) g.off++;
            }
            s16 X = (s16)(x + cc);
            flags = DRVB(D_TEXT_FLAGS);
            s16 fg = (s16)(s8)DRVB(D_TEXT_FG);
            if (pix) {
                if (flags & 0x10) {
                    if ((X + y) & 1) PUT(X, y, fg);
                    else if (flags & 2) PUT((s16)(X + 1), y, fg);
                } else {
                    PUT(X, y, fg);
                    if (DRVB(D_TEXT_FLAGS) & 2) PUT((s16)(X + 1), y, (s16)(s8)DRVB(D_TEXT_FG));
                }
            } else if ((flags & 8) && DSB(f_uline()) == r) {
                PUT(X, y, save);
            }
        }
        if ((DRVB(D_TEXT_FLAGS) & 4) && (r & 1)) x--;
        y++;
        g.off++;
    }
#undef PUT
    DRVB(D_TEXT_FG) = save;
    return w;
}

/* INTRO 05da:340e / PS 16e3:3a22 / AP 1d32:585c — video_library.md §8.4 */
void gfx_text_far(FarPtr s, s16 x, s16 y)
{
    u16 cache = 0;
    u16 wprev = DRVB(D_FONT_W), h = 0;
    if ((s.off | s.seg) == 0) return;
    if ((s8)DRVB(D_TEXT_FLAGS) <= 1 && DRVB(D_CLIP_ON) == 0 && DSB(f_kind()) <= 1) {
        for (; rd8(s.seg, s.off) != 0; s.off++) {
            u8 c = rd8(s.seg, s.off);
            if (wprev > 8) { x = (s16)(x + gfx_char_slow(c, x, y)); continue; }
            s16 i = (s16)(c - DRVB(D_FONT_FIRST));                 /* not range-checked on this path */
            FarPtr g = ds_far(ds_font_glyphs());
            if (font_prop()) {
                FarPtr wid = ds_far(f_wid()), ofs = ds_far(f_ofs());
                wprev = rd8(wid.seg, (u16)(wid.off + i));
                h = DRVB(D_FONT_H);
                g.off = (u16)(g.off + rd16(ofs.seg, (u16)(ofs.off + 2 * i)));
            } else {
                if (cache == 0) {
                    wprev = DRVB(D_FONT_W);
                    h = DRVB(D_FONT_H);
                    cache = (u16)(((wprev + 7) >> 3) * h);
                }
                g.off = (u16)(g.off + (u16)(i * cache));
            }
            ((DrvGlyphFn)drv_entry(1))(y, x, h, g);
            x = (s16)(x + wprev);
        }
        return;
    }
    for (; rd8(s.seg, s.off) != 0; s.off++) {
        u16 w = gfx_char_slow(rd8(s.seg, s.off), x, y);
        x = (s16)(x + w);
        if (DRVB(D_TEXT_FLAGS) & 2) x++;
    }
}

/* INTRO 05da:33f7 / PS 16e3:3a0b / AP 1d32:5845 */
void gfx_text(u16 s_ds, s16 x, s16 y) { gfx_text_far(ds_ptr(s_ds), x, y); }
