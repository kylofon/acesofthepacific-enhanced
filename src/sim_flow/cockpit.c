/* sim_flow: cockpit.c (644a), the panel top line (4899), the .GAU loader and gauge servos (4cad), the
 * LSO signal overlay (50c0, 6dfc:0497/04ce) and the screenshot name (465c:04ee) —
 * sim_flow_cockpit.md §3, §5.1, §8. */
#include "sim_flow/sfa_priv.h"

#define ASSERT_COCKPIT_C far_make(SEG(0x3842), 0x0020)
#define CODE        0x3ECC            /* aircraft code, the .TTM/.EDG/.GAU stem */
#define SHOWN       DSS(0x3ED6)       /* cockpit view shown, -1 none */
#define HAS_GUNNER  DSB(0x3ED8)
#define REBUILDS    DSS(0x3ED9)
#define FORCED      DSB(0x3EDB)
#define IN_EMS      DSB(0x3EEB)
#define TTM_DIR     0x05F2            /* "TTM" (the resource sub-directory) */

static void tag(u16 t) { ttm_run(t); }

/* ---- 4899: panel top line / view clip ---------------------------------------------------------------- */

/* AP 4899:0000 */
s16 fullscreen_view(void)
{
    return (s8)DSB(0x00F8);
}

/* AP 4899:0009 */
void set_panel_top(s16 y)
{
    DSS(0x3CD8) = y;
    DSB(0x00F8) = (s16)(DSS(AP_DS_drv_scr_h) - 1) > y ? 0 : 1;
}

/* AP 4899:0027 -> the clip bottom */
s16 clip_to_view(void)
{
    if (!(s8)DSB(0x00F8)) {
        if (!DSB(AP_DS_drv_clip_on)) {
            DSW(0x26C4) = 0;
            DSW(0x26C6) = (u16)(DSW(AP_DS_drv_scr_w) - 1);
            DSW(0x26C8) = 0;
        }
        DSW(0x26CA) = DSW(0x3CD8);
        DSB(AP_DS_drv_clip_on) = 1;
    }
    return DSS(0x26CA);
}

/* AP 4899:005c limit_window(cam) */
void limit_window(u16 c)
{
    s16 bottom = (s16)(DSS(c + 0x0C) + DSS(c + 0x10) - 1);
    if (bottom > DSS(0x3CD8)) DSW(c + 0x10) = (u16)(DSS(0x3CD8) - DSS(c + 0x0C) + 1);
}

/* ---- 4cad: gauge servos, .GAU file --------------------------------------------------------------------- */

static void servo_setup(u16 s, u16 vmax)
{
    memset(DSP(s), 0, 0x12);
    DSB(s + 0x10) = 2;
    DSW(s + 0x0E) = vmax;
    servo_init(s);
}

/* AP 4cad:0000 */
void gauges_reset(void)
{
    DSW(0x3F94) = 0;
    servo_setup(0x3F96, 0x200);
    servo_setup(0x3FA8, 0x200);
    servo_setup(0x3FBA, 0x100);
    DSB(0x3FCC) = 1;
    DSB(0x3FCD) = 0;
    DSB(0x3FCE) = 0;
    DSB(0x3FEE) &= 0x10;                          /* bits 0-3 and 5-7 cleared */
}

/* AP 4cad:00ab gau_load(code) — sim_flow_cockpit.md §5.1 */
static void gau_load(u16 code)
{
    u16 m = stk_mark();
    u16 face = stk_alloc(0x0E);
    u16 f = my_fopen(res_name(code, 0x05FA /* "GAU" */), 0x0308 /* "rb" */);
    my_fread(0x3F3A, 0x0E, 1, f);
    my_fread(0x3F48, 2, 1, f);
    my_fread(face, 0x0E, 1, f);
    my_fread(0x3F4C, 2, 1, f);
    FarPtr g = mem_far_new((u32)(u16)(DSW(0x3F4C) * 0x2C), 0);
    ds_far_wr(0x3F4E, g);
    memset(DSP(0x3FCF), 0, 0x21);
    memset(DSP(0x3F52), 0, 0x21);
    memset(DSP(0x3F73), 0, 0x21);
    u16 off = 0;
    for (s16 i = 0; i < DSS(0x3F4C); i++, off = (u16)(off + 0x2C)) {
        FarPtr r = far_make(g.seg, (u16)(g.off + off));
        ap_fread_far_55a4(r, 2, f);
        u16 kind = rd16(r.seg, r.off);
        DSB(0x3FCF + ((s16)kind >> 3)) |= (u8)(1 << (kind & 7));
        ap_fread_far_55a4(far_add(r, 2), 0x0E, f);
        set_rect(0, 0, 0, 0, far_add(r, 0x10));
        set_rect(0, 0, 0, 0, far_add(r, 0x18));
        for (u16 k = 0x20; k <= 0x2A; k = (u16)(k + 2)) ap_fread_far_55a4(far_add(r, k), 2, f);
    }
    my_fclose(f);
    res_chdir(0x05FA);
    DSW(0x3F4A) = bmp_load(face);
    res_chdir_up();
    stk_release(m);
}

/* ---- 50c0: LSO signal sprites ------------------------------------------------------------------------ */

/* AP 50c0:0000 */
void lso_load(void)
{
    if (DSB(PLAYER() + 0x26) == 1) DSW(0x11F0) = bmp_load_ems(res_name(0x0502 /* sig_j */, 0x05B4 /* "BMP" */));
    else DSW(0x11F0) = bmp_load_ems(res_name(0x0508 /* sig_a */, 0x05B4));
    heap_hook();
}

/* AP 50c0:004a — DS:11F0 holds the bitmap table from bmp_load_ems, freed with bmp_free_ems
 * (1d32:2170), not a sprite-set index */
void lso_free(void)
{
    if (DSW(0x11F0)) bmp_free_ems(DSW(0x11F0));
    DSW(0x11F0) = 0;
}

/* AP 50c0:0068 */
void lso_reset(void)
{
    DSB(0x5238) = 0;
    DSW(0x523B) = 0xFFFF;
    DSW(0x5236) = 0;
    DSW(0x5239) = 0;
}

/* AP 6dfc:0497 */
void lso_erase(void)
{
    if (DSS(0x523B) >= 0 || !DSB(0x5238)) return;
    DSW(0x26D6) = DSW(0x26D0);
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
    gfx_copy_rect(0xE6, 0x0A, 0x50, 0x3E);
    DSB(0x5238) -= 1;
}

/* AP 6dfc:04ce */
void lso_draw(void)
{
    s16 sig = DSS(0x523B);
    if (sig < 0) return;
    if (DSW(AP_DS_view_inside) && view_get()) return;
    u16 set = DSW(0x11F0);
    if (set) {
        bmp_draw_ems_flip(DSW(set), 0xE6, 0x0A, 0);
        u16 sp;
        s16 flags;
        if (sig == 4) { sp = DSW(set + 0x0A); flags = 0; }
        else if (sig == 7) { sp = DSW(set + 0x0A); flags = 2; }
        else if (sig == 1) { sp = DSW((u16)(set + 2 * sig + 2)); flags = 2; }
        else { sp = DSW((u16)(set + 2 * sig + 2)); flags = 0; }
        bmp_draw_ems_flip(sp, (s16)(0x10A - (DSS(sp + 6) >> 1)), (s16)(0x42 - DSS(sp + 8)), flags);
    }
    DSB(0x5238) = 2;
}

/* ---- 465c:04ee -------------------------------------------------------------------------------------------- */

/* AP 465c:04ee screenshot_name() -> "XXXX__n.LBM" */
u16 screenshot_name(void)
{
    u16 model = DSW((u16)(DSW(0x55D2) + 2 * DSB(0x54BC + DSB(PLAYER() + 0x19))));
    u16 name = (u16)(model + 0x0A);
    u16 len = (u16)(rtl_strlen(name) + 1);
    if (len > 4) len = 4;
    memcpy(DSP(0x3AF4), DSP(name), len);
    memset(DSP(0x3AF4 + len), 0, (size_t)(4 - len));
    memcpy(DSP(0x3AF4 + rtl_strlen(0x3AF4)), DSP(0x00CD), 3);    /* "__" */
    u16 n = DSW(0x00CB);
    DSW(0x00CB) = (u16)(n + 1);
    rtl_itoa((s16)n, 0x3AF8, 10);
    memcpy(DSP(0x3AF4 + rtl_strlen(0x3AF4)), DSP(0x00D0), 5);    /* ".LBM" */
    return 0x3AF4;
}

/* ---- 644a: cockpit.c --------------------------------------------------------------------------------------- */

/* AP 644a:0a35 (body 0a36) cockpit_render_view(v) — near */
static void cockpit_render_view(s16 v)
{
    ap_clip_full();
    switch (v) {
    case 0:
        panel_tiles();
        res_chdir(TTM_DIR); tag(4); res_chdir_up();
        gauge_faces();
        break;
    case 1: res_chdir(TTM_DIR); tag(0x0A); res_chdir_up(); break;
    case 2: res_chdir(TTM_DIR); tag(0x2D); res_chdir_up(); break;
    case 3: res_chdir(TTM_DIR); tag(0x21); res_chdir_up(); break;
    default: break;
    }
}

/* AP 644a:0d94 (body 0d95) cache_to_ems() — near */
static void cache_to_ems(void)
{
    static const u16 handles[3] = { 0x3EEC, 0x3EEE, 0x3EF0 };
    DSW(AP_DS_drv_page_write) = DSW(0x26D0);
    DSW(0x26D6) = DSW(0x26D0);
    for (int i = 0; i < 3; i++) {
        cockpit_render_view((s16)(i + 1));
        FarPtr p = lib_ems_map(DSW(handles[i]), 0);
        gfx_get_rect(p, 0, 0, DSS(AP_DS_drv_scr_w), DSS(AP_DS_drv_scr_h));
        lib_ems_restore_map(DSW(handles[i]));
    }
}

/* AP 644a:0e5e (body 0e5f) ems_to_screen(v) — near */
static void ems_to_screen(s16 v)
{
    static const u16 handles[3] = { 0x3EEC, 0x3EEE, 0x3EF0 };
    if ((u16)v > 3 || v == 0) return;
    FarPtr p = lib_ems_map(DSW(handles[v - 1]), 0);
    gfx_put_rect(p, 0, 0, DSS(AP_DS_drv_scr_w), DSS(AP_DS_drv_scr_h));
    lib_ems_restore_map(DSW(handles[v - 1]));
}

/* AP 644a:1095 (body 1096) screen_to_file(f) — near: the read page row by row */
static void screen_to_file(u16 f)
{
    u16 m = stk_mark();
    u16 row = stk_alloc(0x140);
    for (s16 y = 0; y < DSS(AP_DS_drv_scr_h); y++) {
        gfx_get_rect(ds_ptr(row), 0, y, DSS(AP_DS_drv_scr_w), 1);
        my_fwrite(row, 1, DSS(AP_DS_drv_scr_w), f);
    }
    stk_release(m);
}

/* AP 644a:10e1 (body 10e2) file_rows_to_screen(f) — near */
static void file_rows_to_screen(u16 f)
{
    u16 m = stk_mark();
    u16 row = stk_alloc(0x140);
    for (s16 y = 0; y < DSS(AP_DS_drv_scr_h); y++) {
        my_fread(row, 1, DSS(AP_DS_drv_scr_w), f);
        gfx_put_rect(ds_ptr(row), 0, y, DSS(AP_DS_drv_scr_w), 1);
    }
    stk_release(m);
}

/* AP 644a:0f2a (body 0f2b) cache_to_file(code) — near: CPVW.BUF = 8-byte code + 3 x 64000 */
static void cache_to_file(u16 code)
{
    u16 m = stk_mark();
    u16 hdr = stk_alloc(0x0A);
    u16 f = my_fopen(0x0F38 /* "cpvw.buf" */, 0x0F85 /* "rb" */);
    my_fread(hdr, 8, 1, f);
    my_fclose(f);
    if (rtl_stricmp(hdr, code) != 0) {
        f = my_fopen(0x0F38, 0x0F88 /* "wb" */);
        u16 n = rtl_strlen(code) + 1;                 /* strncpy(hdr, code, 8) */
        if (n > 8) n = 8;
        memcpy(DSP(hdr), DSP(code), n);
        memset(DSP(hdr + n), 0, (size_t)(8 - n));
        DSB(hdr + 8) = 0;
        my_fwrite(hdr, 8, 1, f);
        DSW(AP_DS_drv_page_write) = DSW(0x26D0);
        DSW(0x26D6) = DSW(0x26D0);
        for (s16 v = 1; v <= 3; v++) {
            cockpit_render_view(v);
            screen_to_file(f);
        }
        my_fclose(f);
    }
    stk_release(m);
}

/* AP 644a:101f (body 1020) file_to_screen(v) — near */
static void file_to_screen(s16 v)
{
    u16 f = my_fopen(0x0F38 /* "cpvw.buf" */, 0x0F8B /* "rb" */);
    switch (v) {
    case 1: my_fseek(f, 8, 0); break;
    case 2: my_fseek(f, 0xFA08, 0); break;
    case 3: my_fseek(f, 0x1F408, 0); break;
    default: break;
    }
    file_rows_to_screen(f);
    my_fclose(f);
}

/* AP 644a:09db (body 09dc) cockpit_view_background(v) — near */
static void cockpit_view_background(s16 v)
{
    if ((u16)v > 3) return;
    if (v == 0) {
        DSW(0x26D6) = (u16)(DSW(0x26D0) + 0x400);
        gfx_copy_rect(0, 0, DSS(AP_DS_drv_scr_w), DSS(AP_DS_drv_scr_h));
    } else if (IN_EMS) {
        ems_to_screen(v);
    } else {
        file_to_screen(v);
    }
}

/* AP 644a:09b0 cockpit_prebuild() — near */
static void cockpit_prebuild(void)
{
    ap_clip_full();
    DSW(AP_DS_drv_page_write) = (u16)(DSW(0x26D0) + 0x400);
    cockpit_render_view(0);
    if (IN_EMS) cache_to_ems();
    else cache_to_file(CODE);
}

/* AP 644a:0912 (body 0913) palette_aces — ACES.PAL over the sim palette */
static void palette_file_apply(u16 name)
{
    u32 p = pal_load(res_name(name, 0x05CC /* "PAL" */));
    FarPtr pf = { (u16)p, (u16)(p >> 16) };
    rtl__fmemcpy(ds_far(AP_DS_sim_palette), pf, 0x300);
    pal_free(pf);
    pal_fx_update(1);
}

void palette_aces(void)
{
    palette_file_apply(0x0F79 /* "ACES" */);
}

/* AP 644a:0961 (body 0962) palette_gunner */
static void palette_gunner(void)
{
    palette_file_apply(0x0F7E /* "GUNNER" */);
}

/* AP 644a:0000 cockpit_load(code) — sim_flow_cockpit.md §3 */
void cockpit_load(u16 code)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x20);
    DSB(0x3EF2) = rtl_stricmp(code, 0x0F41 /* "P39" */) == 0;
    DSB(0x3EF3) = rtl_stricmp(code, 0x0F45 /* "J7W" */) == 0;
    DSB(0x3EF4) = rtl_stricmp(code, 0x0F49 /* "P80" */) == 0;
    HAS_GUNNER = 0;
    rtl_strcpy(CODE, code);
    ttm_init();
    if (ttm_load(res_name(CODE, TTM_DIR)) == -1) assert_fail(ASSERT_COCKPIT_C, 0xA8, CODE);
    ttm_index(0);
    res_chdir(TTM_DIR);
    tag(1);
    res_chdir_up();
    static const u16 fmts[4] = { 0x0F4D /* F%s */, 0x0F51 /* B%s */, 0x0F55 /* L%s */, 0x0F59 /* R%s */ };
    s16 s = 0;
    for (int i = 0; i < 4; i++) {
        rtl_sprintf(buf, fmts[i], (unsigned)CODE);
        s16 z = file_size(buf);
        if (i == 0) s = z;
        else s = z < s ? s : file_size(buf);
    }
    rtl_sprintf(buf, 0x0F5D /* D%s */, (unsigned)CODE);
    s16 d = file_size(buf);
    if (d) {
        s = s > d ? s : d;
        HAS_GUNNER = 1;
    }
    edge_arena_init((s16)(s + 0x340));
    gau_load(CODE);
    if (DSC(AP_DS_ems_tier) >= 4) {
        IN_EMS = 1;
        DSW(0x3EEC) = lib_ems_alloc(0xFA00);
        if (DSW(0x3EEC) == 0) {
            IN_EMS = 0;
        } else {
            DSW(0x3EEE) = lib_ems_alloc(0xFA00);
            if (DSW(0x3EEE) == 0) {
                IN_EMS = 0;
                lib_ems_free(DSW(0x3EEC));
            } else {
                DSW(0x3EF0) = lib_ems_alloc(0xFA00);
                if (DSW(0x3EF0) == 0) {
                    IN_EMS = 0;
                    lib_ems_free(DSW(0x3EEE));
                    lib_ems_free(DSW(0x3EEC));
                }
            }
        }
    } else {
        IN_EMS = 0;
    }
    cockpit_prebuild();
    stk_release(m);
}

/* enter a view: background, tags, window outlines */
static void edges(u16 fmt)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x14);
    rtl_sprintf(buf, fmt, (unsigned)CODE);
    edge_load(buf);
    stk_release(m);
}

/* AP 644a:0271 cockpit_frame_bg(redraw) */
void cockpit_frame_bg(s16 redraw)
{
    if (DSW(0x3EF5) != DSW(0x3EDC) && SHOWN == 1) {
        DSW(0x3EDC) = DSW(0x3EF5);
        REBUILDS = 2;
        FORCED = 1;
    }
    s16 r;
    if (REBUILDS) {
        r = REBUILDS;
        REBUILDS -= 1;
    } else {
        r = redraw;
        FORCED = 0;
    }
    if (r == 2) {
        s16 v = view_get();
        if (v != SHOWN || FORCED) {
            switch (SHOWN) {                                   /* leave the old view */
            case 0: edge_unload(); tag(7); break;
            case 1:
                edge_unload(); tag(0x15);
                if (HAS_GUNNER && !FORCED) palette_aces();
                break;
            case 2: edge_unload(); tag(0x36); break;
            case 3: edge_unload(); tag(0x2A); break;
            default: break;
            }
            SHOWN = v;
            DSW(AP_DS_drv_page_write) = DSW(0x26D0);
            ap_clip_full();
            switch (SHOWN) {                                   /* enter the new view */
            case 0:
                cockpit_view_background(0);
                res_chdir(TTM_DIR); tag(5); res_chdir_up();
                edges(0x0F61);
                break;
            case 1:
                if (HAS_GUNNER) {
                    if (!FORCED) palette_gunner();
                    if (DSW(0x3EF5) == 1) {
                        cockpit_view_background(1);
                        res_chdir(TTM_DIR); tag(0x14); tag(0x0B); res_chdir_up();
                        edges(0x0F65);
                    } else {
                        cockpit_view_background(1);
                        res_chdir(TTM_DIR); tag(0x13); tag(0x0B); res_chdir_up();
                        edges(0x0F69);
                    }
                } else {
                    cockpit_view_background(1);
                    res_chdir(TTM_DIR); tag(0x0B); res_chdir_up();
                    edges(0x0F6D);
                }
                break;
            case 2:
                cockpit_view_background(2);
                res_chdir(TTM_DIR); tag(0x2E); res_chdir_up();
                edges(0x0F71);
                break;
            case 3:
                cockpit_view_background(3);
                res_chdir(TTM_DIR); tag(0x22); res_chdir_up();
                edges(0x0F75);
                break;
            default:
                return;
            }
        }
    } else if (r != 1) {
        return;
    }
    if ((u16)SHOWN > 3) return;
    DSW(0x26D6) = DSW(0x26D0);
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
    ap_clip_full();
    clip_to_view();
    gfx_copy_rect(DSS(0x26C4), DSS(0x26C8), (s16)(DSS(0x26C6) - DSS(0x26C4) + 1), (s16)(DSS(0x26CA) - DSS(0x26C8) + 1));
}

/* AP 644a:0ad9 damage_cracks() — near */
static void damage_cracks(void)
{
    if (SHOWN == 0)
        for (s16 i = 0; i < DSS(0x3EE9); i++) tag((u16)(0x4D + i));
    if (DSS(0x3EE9) < 7) DSW(0x3EE9) += 1;
}

/* AP 644a:0b0b (body 0b0c) engine_puffs() — near; sim_flow_cockpit.md §3.3 */
static void engine_puffs(void)
{
    u16 model = DSW((u16)(DSW(0x55D2) + 2 * DSB(0x54BC + DSB(PLAYER() + 0x19))));
    u8 flags = DSB(model + 0x53);
    if (SHOWN == 0) {
        if (DSS((u16)(DSW(PLAYER() + 0x66) + 0x90)) > 1 || (flags & 1) || DSB(0x3EF2)) return;
    }
    if (SHOWN == 2 || SHOWN == 3) {
        if ((flags & 2) || DSB(0x3EF2)) return;
    }
    u16 set = DSW((u16)(DSW(0x519C) + 0x7A));
    if (DSW(0x3EDF) != DSW(0x3EFB) || DSB(0x3EDE)) {
        DSW(0x3EDF) = DSW(0x3EFB);
        DSB(0x3EDE) = 0;
        switch (DSW(0x3EFB)) {
        case 0: DSW(0x3EE5) = 0xFFFF; DSW(0x3EE7) = 0; break;
        case 1:
            DSW(0x3EE5) = (u16)((rtl_rand() & 0x0F) + 1);
            DSW(0x3EE7) = (u16)((rtl_rand() & 3) + 1);
            break;
        case 2:
            DSW(0x3EE5) = (u16)((rtl_rand() & 3) + 1);
            DSW(0x3EE7) = (u16)((rtl_rand() & 0x0F) + 1);
            break;
        case 3: DSW(0x3EE5) = 0; DSW(0x3EE7) = 0xFFFF; break;
        default: break;
        }
    }
    u16 e;
    if (SHOWN == 0) e = (u16)(0x0F18 + 4 * DSW(0x3EE1));
    else if (SHOWN == 2 || SHOWN == 3) e = (u16)(0x0F28 + 4 * DSW(0x3EE1));
    else e = 0;                                                    /* (not reached: views 0, 2, 3 only) */
    if (DSS(e) <= DSS(0x3EE3)) {
        DSW(0x3EE3) = 0;
        switch (DSW(0x3EE1)) {
        case 0:
            if (DSS(0x3EE5) == -1) break;
            if (DSW(0x3EE5)) DSW(0x3EE5) -= 1;
            else { DSW(0x3EE1) = 1; e = (u16)(e + 4); }
            break;
        case 1: DSW(0x3EE1) = 2; e = (u16)(e + 4); break;
        case 2:
            if (DSS(0x3EE7) == -1) break;
            if (DSW(0x3EE7)) DSW(0x3EE7) -= 1;
            else { DSW(0x3EE1) = 3; e = (u16)(e + 4); }
            break;
        case 3: DSW(0x3EE1) = 0; e = 0x0F18; DSB(0x3EDE) = 1; break;
        default: break;
        }
    }
    if (DSW(e + 2) == 0) return;
    u16 fr = (u16)(DSW(e + 2) + 4 * DSW(0x3EE3));
    u16 p = DSW(fr + 2);
    s16 n = DSS(fr);
    for (s16 i = 0; i < n; i++, p = (u16)(p + 6)) {
        s16 x = DSS(p), y = DSS(p + 2), fl = DSS(p + 4);
        switch (SHOWN) {
        case 0:
            bmp_draw_ems_flip(set, x, (s16)(y + 7), fl);
            bmp_draw_ems_flip(set, (s16)(0x13F - x - DSS(set + 6)), (s16)(y + 7), (s16)(fl ^ 2));
            break;
        case 2:
            bmp_draw_ems_flip(set, x, y, fl);
            break;
        case 3:
            bmp_draw_ems_flip(set, (s16)(0x13F - x - DSS(set + 6)), y, (s16)(fl ^ 2));
            break;
        default:
            break;
        }
    }
    DSW(0x3EE3) += 1;
}

/* AP 644a:0584 cockpit_frame_fg(redraw) */
void cockpit_frame_fg(s16 redraw)
{
    ap_clip_full();
    clip_to_view();
    DSW(0x26D6) = DSW(0x26D0);
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
    u16 cb = (u16)(PLAYER() + 0xAC);
    switch (SHOWN) {
    case 0:
        mask_on();
        engine_puffs();
        if (DSB(0x3EF3)) tag(DSS(cb + 2) < 0 ? 0x57 : DSS(cb + 2) > 0 ? 0x55 : 0x56);
        mask_off();
        glass_overlay();
        switch (DSW(0x3EF9)) {
        case 3: tag(0x1A); /* fall through */
        case 2: tag(0x19); /* fall through */
        case 1: tag(0x18); break;
        default: break;
        }
        tag(6);
        break;
    case 1:
        mask_on();
        tag(DSS(cb + 4) < 0 ? 0x0F : DSS(cb + 4) > 0 ? 0x0D : 0x0E);
        tag(DSS(cb + 2) < 0 ? 0x12 : DSS(cb + 2) > 0 ? 0x10 : 0x11);
        tag(0x0C);
        mask_off();
        glass_overlay();
        switch (DSW(0x3EF9)) {
        case 3: tag(0x1D); /* fall through */
        case 2: tag(0x1C); /* fall through */
        case 1: tag(0x1B); break;
        default: break;
        }
        break;
    case 2:
        mask_on();
        engine_puffs();
        tag(DSS(cb) < 0 ? 0x30 : DSS(cb) > 0 ? 0x32 : 0x31);
        tag((DSB(cb + 0x0E) & 3) ? 0x34 : 0x33);
        if (!DSB(0x3EF4) || (DSB(PLAYER() + 0x1A) & 4)) tag(0x2F);
        mask_off();
        glass_overlay();
        if (DSB(0x3EF8)) tag(0x42);
        switch (DSW(0x3EF9)) {
        case 3: tag(0x3E); /* fall through */
        case 2: tag(0x3D); /* fall through */
        case 1: tag(0x3C); break;
        default: break;
        }
        break;
    case 3:
        mask_on();
        engine_puffs();
        tag(DSS(cb) < 0 ? 0x26 : DSS(cb) > 0 ? 0x24 : 0x25);
        tag((DSB(cb + 0x0E) & 3) ? 0x28 : 0x27);
        if (!DSB(0x3EF4) || (DSB(PLAYER() + 0x1A) & 4)) tag(0x23);
        mask_off();
        glass_overlay();
        if (DSB(0x3EF8)) tag(0x41);
        switch (DSW(0x3EF9)) {
        case 3: tag(0x3B); /* fall through */
        case 2: tag(0x3A); /* fall through */
        case 1: tag(0x39); break;
        default: break;
        }
        break;
    default:
        break;
    }
    gauges_draw(redraw);
    if (DSB(0x3EF7)) {
        gauges_freeze();
        damage_cracks();
    }
}

/* AP 644a:088f */
void cockpit_force_rebuild(void)
{
    REBUILDS = 2;
    FORCED = 1;
}

/* AP 644a:089f cockpit_view_window(r) -> the eye row */
s16 cockpit_view_window(u16 r)
{
    DSW(r + 2) = 0;
    DSW(r) = 0;
    DSW(r + 4) = 0x140;
    s16 v = view_get();
    if ((u16)v <= 3) {
        u16 model = DSW((u16)(DSW(0x55D2) + 2 * DSB(0x54BC + DSB(PLAYER() + 0x19))));
        DSW(r + 6) = DSW((u16)(model + 2 * v + 0x28));
        return DSS((u16)(model + 2 * v + 0x30));
    }
    DSW(r + 6) = 0xC8;
    return 0x64;
}
