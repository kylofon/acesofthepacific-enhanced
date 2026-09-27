/* shell_ui part B: custmiss.c (PS 147e) — custom missions (shell_ui_mission.md §7). Screen 60
 * PARTS/CUSTMISS, table 4c32:0011 (11 items). Lists CMS\*.CMD; none are shipped, so the fly-menu
 * button is hidden and this screen is unreachable with the shipped data (kept faithful). */
#include "shell_ui/sui_b_priv.h"

#define CM_SEG      0x4c32
#define CM_TBL      CM_SEG, 0x0011
#define CM_CHOSEN   0x4F04      /* char[9]: chosen mission name */
#define CM_NAMES    0x4F0D      /* near char *[] */
#define CM_BLOCK    0x4F0F      /* 9 bytes per name */
#define CM_DPAGE    0x4F11      /* first entry of the 5-entry description cache */
#define CM_HPAGE    0x4F13      /* first entry of the 16-entry header cache */
#define CM_COUNT    0x4F15
#define CM_BUTTON   0x4F17
#define CM_AIRCRAFT 0x4F19      /* near: u16[16] */
#define CM_NODESC   0x4F1B      /* "No description." */
#define CM_TITLES   0x4F1D      /* near: char[16][25] */
#define CM_DESCS    0x4F1F      /* near: char[5][500] */
#define CM_PATTERN  0x0EF8      /* near ptr -> "cms\*.cmd" */
#define CM_EXT      0x0EFA      /* near ptr -> ".cmt" */

static u16 cm_open(u16 path, u16 full, u16 mode1, u16 mode2)
{
    rtl_strcpy(full, 0x40CA);                        /* work directory first */
    rtl_strcat(full, path);
    u16 f = rtl_fopen(full, mode1);
    if (f == 0) f = rtl_fopen(path, mode2);
    return f;
}

/* PS 147e:000c cm_load_headers */
static void cm_load_headers(void)
{
    u16 mk = stk_mark();
    u16 full = stk_alloc(0x68), hdr = stk_alloc(0x1C), path = stk_alloc(0x14);
    for (int b = 0; b < 0x14; b++) DSB(path + b) = DSB(0x0EFC + b);   /* "cms\" */
    u16 title = DSW(CM_TITLES);
    for (s16 i = 0; i < 16; i++, title = (u16)(title + 0x19)) {
        s16 k = (s16)(i + (s16)DSW(CM_HPAGE));
        if (k >= (s16)DSW(CM_COUNT)) break;
        DSB(path + 4) = 0;
        rtl_strcat(path, DSW((u16)(DSW(CM_NAMES) + k * 2)));
        rtl_strcat(path, DSW(CM_EXT));
        u16 f = cm_open(path, full, 0x0F37, 0x0F3A);
        if (f == 0) {
            DSW((u16)(DSW(CM_AIRCRAFT) + i * 2)) = 0xFFFF;
            continue;
        }
        rtl_fread(hdr, 0x1B, 1, f);
        DSW((u16)(DSW(CM_AIRCRAFT) + i * 2)) = DSW(hdr);
        rtl_strncpy(title, (u16)(hdr + 2), 0x19);
        DSB(title + 0x18) = 0;
        rtl_fclose(f);
    }
    stk_release(mk);
}

/* PS 147e:011f cm_load_descs */
static void cm_load_descs(void)
{
    u16 mk = stk_mark();
    u16 buf = stk_alloc(0x210), full = stk_alloc(0x50), path = stk_alloc(0x14);
    for (int b = 0; b < 0x14; b++) DSB(path + b) = DSB(0x0F10 + b);   /* "cms\" */
    u16 desc = DSW(CM_DESCS);
    for (s16 i = 0; i < 5; i++, desc = (u16)(desc + 0x1F4)) {
        s16 k = (s16)(i + (s16)DSW(CM_DPAGE));
        if (k >= (s16)DSW(CM_COUNT)) break;
        DSB(path + 4) = 0;
        rtl_strcat(path, DSW((u16)(DSW(CM_NAMES) + k * 2)));
        rtl_strcat(path, DSW(CM_EXT));
        u16 f = cm_open(path, full, 0x0F3D, 0x0F40);
        if (f == 0) continue;
        rtl_fread(buf, 0x20F, 1, f);
        u16 p = (u16)(rtl_strchr((u16)(buf + 2), 0) + 1);      /* the text after the title */
        rtl_strncpy(desc, p, 0x1F4);
        DSB(desc + 0x1F3) = 0;
        rtl_fclose(f);
    }
    stk_release(mk);
}

/* PS 147e:0225 cm_page_headers(i) */
static void cm_page_headers(s16 i)
{
    if (i < 0 || i >= (s16)DSW(CM_COUNT)) ufatal(CM_SEG, 0xFF);
    if ((s16)(i - (s16)DSW(CM_HPAGE)) >= 16 || i < (s16)DSW(CM_HPAGE)) {
        DSW(CM_HPAGE) = (u16)((i / 16) << 4);
        cm_load_headers();
    }
}

/* PS 147e:027f cm_page_descs(i) */
static void cm_page_descs(s16 i)
{
    if (i < 0 || i >= (s16)DSW(CM_COUNT)) ufatal(CM_SEG, 0x111);
    if ((s16)(i - (s16)DSW(CM_DPAGE)) >= 5 || i < (s16)DSW(CM_DPAGE)) {
        DSW(CM_DPAGE) = (u16)((i / 5) * 5);
        cm_load_descs();
    }
}

static void cm_check_hdr(s16 i, u16 line)
{
    if ((s16)(i - (s16)DSW(CM_HPAGE)) >= 16 || i < (s16)DSW(CM_HPAGE)) ufatal(CM_SEG, line);
}

/* PS 147e:02da cm_has_header(i) */
static u8 cm_has_header(s16 i)
{
    cm_check_hdr(i, 0x124);
    return (s16)DSW((u16)(DSW(CM_AIRCRAFT) + (i - (s16)DSW(CM_HPAGE)) * 2)) > -1;
}

/* PS 147e:0333 cm_aircraft(i) */
static s16 cm_aircraft(s16 i)
{
    cm_check_hdr(i, 0x132);
    return (s16)DSW((u16)(DSW(CM_AIRCRAFT) + (i - (s16)DSW(CM_HPAGE)) * 2));
}

/* PS 147e:0382 cm_title(i) */
static u16 cm_title(s16 i)
{
    cm_check_hdr(i, 0x140);
    return (u16)(DSW(CM_TITLES) + (i - (s16)DSW(CM_HPAGE)) * 0x19);
}

/* PS 147e:03d3 cm_desc(i) */
static u16 cm_desc(s16 i)
{
    if ((s16)(i - (s16)DSW(CM_DPAGE)) >= 5 || i < (s16)DSW(CM_DPAGE)) ufatal(CM_SEG, 0x14E);
    return (u16)(DSW(CM_DESCS) + (i - (s16)DSW(CM_DPAGE)) * 0x1F4);
}

/* PS 147e:0424 cm_draw_window — the service/date arguments are two constants */
static void cm_draw_window(void)
{
    u16 lbl = uw(cur_item(), HS_LABEL);
    draw_side_riveted_panel();
    draw_title_bar_for(0, 0x3BD4, lbl);
}

/* PS 147e:0450 cm_draw_label */
static void cm_draw_label(void)
{
    u16 lbl = uw(cur_item(), HS_LABEL);
    gfx_font_select((s16)DSW(F_FONT3));
    text_engrave(lbl, 0xC1, 0xB1, 0xC0, DSW(RX), DSW(RY));
}

/* PS 147e:048e cm_draw_scroll */
static void cm_draw_scroll(void)
{
    draw_bevel_box();
    DSB(LINE_C) = 0xC0;
    gfx_rect((s16)(DSS(RX) + 3), (s16)(DSS(RY) + 0x17), 0x13, 0x0B);
}

/* PS 147e:04bb cm_draw_row(i, y) */
static void cm_draw_row(s16 i, s16 y)
{
    u16 mk = stk_mark();
    u16 buf = stk_alloc(0x2C);
    cm_page_headers(i);
    if (cm_has_header(i)) {
        u16 t = cm_title(i);
        s16 ac = cm_aircraft(i);
        str_upper(t);
        str_upper(strcpy_far_src(buf, ac_name(ac)).off);
        gfx_text(t, (s16)(DSS(RX) + 3), y);
        gfx_text(buf, (s16)(DSS(RX) + 0x96), y);
    } else {
        gfx_text(DSW((u16)(DSW(CM_NAMES) + i * 2)), (s16)(DSS(RX) + 3), y);
    }
    set_mask(hs_item(CM_TBL, 7), 3);                   /* description redraw */
    stk_release(mk);
}

/* PS 147e:0560 cm_draw_desc */
static void cm_draw_desc(void)
{
    s16 sel = (s16)lbox_selected();
    cm_page_descs(sel);
    u16 text = cm_has_header(sel) ? cm_desc(sel) : DSW(CM_NODESC);
    DSB(FILL_ON) = 1;
    DSB(CLIP_ON) = 0;
    DSB(FILL_C) = 0xB8;
    DSB(LINE_C) = 0xB8;
    gfx_rect(DSS(RX), DSS(RY), DSS(RW), DSS(RH));
    gfx_font_select((s16)DSW(F_FONT3));
    flow_setup(3, 7, 1);
    flow_text(ds_ptr(text), 1, fnull());
    set_mask(cur_item(), 0x40);
}

/* PS 147e:05f6 cm_btn_cb */
static void cm_btn_cb(void)
{
    if (btn_cb()) {
        DSW(CM_BUTTON) = DSW(CUR_IDX);
        menu_set_result(DSW(CUR_IDX) == 8 ? 2 : 1);
    }
}

/* PS 147e:061e base_name(dst, src) — up to the first '.' */
static u16 base_name(u16 dst, u16 src)
{
    u16 d = dst;
    while (DSB(src) != 0 && DSB(src) != '.') DSB(d++) = DSB(src++);
    DSB(d) = 0;
    return dst;
}

/* PS 147e:0648 name_cmp(a, b) — strcmp(*a, *b); the original does not return it explicitly (AX
 * happens to hold strcmp's value) */
static s16 name_cmp(u16 a_ds, u16 b_ds)
{
    return rtl_strcmp(DSW(a_ds), DSW(b_ds));
}

/* PS 147e:0664 cm_scan(desc_buf, title_buf, aircraft_buf) */
static void cm_scan(u16 desc, u16 title, u16 aircraft)
{
    u16 mk = stk_mark();
    u16 ff = stk_alloc(0x2C), base = stk_alloc(0x0A);     /* ffblk (name at +1Eh), base name */
    DSW(CM_DESCS) = desc;
    DSW(CM_TITLES) = title;
    DSW(CM_AIRCRAFT) = aircraft;
    if (rtl_findfirst(DSW(CM_PATTERN), ff, 0) != 0) {
        DSW(CM_COUNT) = 0;
        stk_release(mk);
        return;
    }
    DSW(CM_COUNT) = 1;
    while (rtl_findnext(ff) == 0) DSW(CM_COUNT) = (u16)(DSW(CM_COUNT) + 1);
    DSW(CM_NAMES) = xmalloc((u16)(DSW(CM_COUNT) << 1));
    DSW(CM_BLOCK) = xmalloc((u16)(DSW(CM_COUNT) * 9));
    DSW(DSW(CM_NAMES)) = DSW(CM_BLOCK);
    for (s16 i = 1; i < (s16)DSW(CM_COUNT); i++)
        DSW((u16)(DSW(CM_NAMES) + i * 2)) = (u16)(DSW(CM_BLOCK) + i * 9);
    rtl_findfirst(DSW(CM_PATTERN), ff, 0);
    rtl_strcpy(DSW(CM_BLOCK), base_name(base, (u16)(ff + 0x1E)));
    for (s16 i = 1; rtl_findnext(ff) == 0; i++)
        rtl_strcpy((u16)(DSW(CM_BLOCK) + i * 9), base_name(base, (u16)(ff + 0x1E)));
    rtl_qsort(DSW(CM_NAMES), DSW(CM_COUNT), 2, UFP(name_cmp));
    DSW(CM_DPAGE) = 0;
    DSW(CM_HPAGE) = 0;
    cm_load_headers();
    cm_load_descs();
    stk_release(mk);
}

/* PS 147e:07a1 cm_free */
static void cm_free(void)
{
    xfree(DSW(CM_NAMES));
    xfree(DSW(CM_BLOCK));
}

/* PS 147e:07ba cm_draw_bg */
static void cm_draw_bg(void)
{
    screen_clear_panel();
}

/* PS 147e:07c4 cm_frame */
static void cm_frame(void)
{
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
}

/* PS 147e:07e5 custom_missions -> near name / 0 */
u16 custom_missions(void)
{
    u8 r = 0;
    u16 mk = stk_mark();
    u16 aircraft = stk_alloc(0x20), titles = stk_alloc(0x190), descs = stk_alloc(0x9C4);
    air_open();
    DSW(CM_BUTTON) = 10;
    cm_scan(descs, titles, aircraft);
    if (DSW(CM_COUNT) == 0) ufatal(CM_SEG, 0x27D);
    while (r == 0) {
        lbox_setup(0, 0, DSW(CM_COUNT), 8, fnull(), UFP(cm_draw_row));
        lbox_bind(fdat(CM_TBL), 3, 5);
        screen_open(0x3C, UFP(cm_draw_bg), 0x89, 1);
        DSW(CM_NODESC) = str_get(6);                     /* "No description." */
        hs_set_table(fdat(CM_TBL), 0x0B);
        hs_focus(DSW(CM_BUTTON));
        hs_draw_all();
        r = (u8)menu_run(UFP(cm_frame));
        ui_busy();
        if (DSW(CM_BUTTON) == 9) {                       /* REALISM PANEL */
            realism_run();
            r = 0;
        } else if (DSW(CM_BUTTON) == 10) {               /* BEGIN MISSION */
            rtl_strcpy(CM_CHOSEN, DSW((u16)(DSW(CM_NAMES) + lbox_selected() * 2)));
        }
    }
    cm_free();
    air_close();
    stk_release(mk);
    return DSW(CM_BUTTON) == 10 ? CM_CHOSEN : 0;
}

/* PS 147e:0935 custmiss_exist */
u16 custmiss_exist(void)
{
    u16 mk = stk_mark();
    u16 ff = stk_alloc(0x2C);
    s16 r = rtl_findfirst(DSW(CM_PATTERN), ff, 0);
    stk_release(mk);
    return r == 0;
}

__attribute__((constructor)) static void custmiss_codeptrs(void)
{
    UREG(cm_draw_window);
    UREG(cm_draw_label);
    UREG(cm_draw_scroll);
    UREG(cm_draw_row);
    UREG(cm_draw_desc);
    UREG(cm_btn_cb);
    UREG(name_cmp);
    UREG(cm_draw_bg);
    UREG(cm_frame);
}
