/* sim_flow: the radio order menu — item builders and order dispatch (1660 sim_flow subset) and the
 * menu box (6e5a). sim_flow_cockpit.md §7, sim_flow_radio.md. */
#include "sim_flow/sfa_priv.h"

#define PAGE     DSS(0x5BD0)
#define SUBMENU  DSW(0x5BCE)
#define STEP     DSW(0x5BD2)
#define TGT(i)   ((u16)(0x5B6C + 12 * (i)))
#define LINES    DSS(0x5240)
#define LINE(i)  ((u16)(0x524A + 0x3E * (i)))

static u16 dat_plane(u16 o) { return DSW((u16)(DSW(0x55D2) + 2 * DSB((u16)(0x54BC + DSB(o + 0x19))))); }
static u16 dat_ship(u16 o) { return DSW((u16)(DSW(0x55D0) + 2 * DSB((u16)(0x555B + DSB(o + 0x19))))); }

/* AP 1660:0685 order_text(code, index, dst) — STR(7, code) expanded against the order target */
void order_text(s16 code, s16 index, u16 dst)
{
    u16 m = stk_mark();
    u16 ctx = stk_alloc(0x18), name = stk_alloc(0x16);
    rtl__fstrcpy(ds_ptr(dst), str(7, code));
    DSW(ctx + 0x16) = 0;
    if (code == 0x40 || code == 0x41 || code == 0x42) {
        u16 rec = TGT(index);
        u16 n = aircraft_name(rec, name);
        ds_far_wr((u16)(ctx + 0x0E), ds_ptr(n));
        DSL(ctx + 2) = DSL(rec + 2);
        DSW(ctx) = DSW(rec + 6);
        DSB(ctx + 0x0C) = DSB(rec + 8);
    }
    while (radio_expand(dst, ctx)) {}
    stk_release(m);
}

static void set_target(u16 rec, u8 kind, u8 b0, u16 obj)
{
    DSB(rec + 1) = kind;
    DSB(rec) = b0;
    DSW(rec + 0x0A) = obj;
    DSW(rec + 6) = (u16)(bearing_between((u16)(obj + 4), (u16)(PLAYER() + 4)) - DSW(PLAYER() + 0x14));
    DSL(rec + 2) = 0xFFFFD8F0;
    DSW(rec + 8) = 1;
}

/* AP 1660:07bb targets_list(items, n, air) -> new n. Quirk kept: only the ship loop checks n >= 8. */
s16 targets_list(u16 items, s16 n, s16 air)
{
    u16 m0 = stk_mark();
    u16 mates = stk_alloc(0x40), spotted = stk_alloc(0x84), cand = stk_alloc(0x100), cnt = stk_alloc(8);
    u16 p = PLAYER();
    s16 fighter = DSB(dat_plane(p) + 0x38) == 0;
    s16 armed = 0;
    s16 addon = addon1_flag();
    s16 k = (s16)mates_of(p, PAGE, mates);
    for (s16 i = 0; i < k; i++) {
        u16 mm = DSW((u16)(mates + 2 * i));
        if (has_ordnance(mm) || (addon && rockets_left(mm))) armed = 1;
    }
    if (air || fighter) {
        memset(DSP(cnt), 0, 8);
        s16 e = (s16)spotted_split(spotted, p, 0);
        for (s16 i = 0; i < e; i++) {
            u16 q = DSW((u16)(spotted + 2 * i));
            DSW((u16)(cnt + 2 * (s16)DSC(dat_plane(q) + 0x38))) += 1;
        }
        for (s16 r = 0; r < 4; r++) {
            u16 c = DSW((u16)(cnt + 2 * r));
            if (!c) continue;
            DSW((u16)(items + 2 * n)) = 0x40;
            u16 rec = TGT(n);
            n++;
            DSB(rec + 1) = 0;
            DSB(rec) = (u8)r;
            DSW(rec + 8) = c;
            DSW(rec + 0x0A) = 0;
        }
    }
    u16 code = air ? 0x41 : 0x42;
    u16 pl = DSW(0x4755);
    for (s16 i = 0; i < DSS(0x476E); i++, pl = (u16)(pl + 0x20)) {
        if (DSC(pl + 0x1C) > 7) continue;
        if ((s16)(s8)plane_c0((u16)(s16)DSC(pl + 0x1D)) == (s16)DSC(p + 0x26)) continue;
        if (ldist2((u16)(pl + 4), (u16)(p + 4)) >= 49000) continue;
        DSW((u16)(items + 2 * n)) = code;
        set_target(TGT(n), 2, DSB(pl + 0x1C), pl);
        n++;
    }
    s16 nc = 0;
    for (s16 i = 0; i < DSS(0x4774); i++) {
        u16 s = DSW((u16)(DSW(0x4759) + 2 * i));
        if (DSB(s + 0x1B) & 2) continue;
        if (!armed && DSB(dat_ship(s) + 0x6D) == 0) continue;
        if (DSC(s + 0x26) == DSC(p + 0x26)) continue;
        if (ldist2((u16)(s + 4), (u16)(p + 4)) >= 49000) continue;
        DSW((u16)(cand + 2 * nc)) = s;
        nc++;
    }
    for (s16 j = 0; j < 9; j++) {
        s16 cls = DSS((u16)(0x18EC + 2 * j));
        for (s16 i = 0; i < nc; i++) {
            u16 s = DSW((u16)(cand + 2 * i));
            if ((s16)DSC(dat_ship(s) + 0x6C) != cls) continue;
            if (n >= 8) { stk_release(m0); return n; }
            DSW((u16)(items + 2 * n)) = code;
            set_target(TGT(n), 1, (u8)cls, s);
            n++;
        }
    }
    stk_release(m0);
    return n;
}

/* AP 1660:0bae targets_exist(bombers) */
s16 targets_exist(s16 bombers)
{
    u16 m = stk_mark();
    u16 tmp = stk_alloc(0x28);
    s16 r = targets_list(tmp, 0, bombers == 0) > 0;
    stk_release(m);
    return r;
}

/* AP 1660:0c12 add_attack_item(items, n, bombers) */
s16 add_attack_item(u16 items, s16 n, s16 bombers)
{
    if (targets_exist(bombers)) {
        DSW((u16)(items + 2 * n)) = bombers ? 0x32 : 0x31;
        n++;
    }
    return n;
}

static void put(u16 items, s16 *n, u16 code) { DSW((u16)(items + 2 * *n)) = code; (*n)++; }

/* AP 1660:0c47 orders_list(items, max, near, k) — sim_flow_radio.md */
s16 orders_list(u16 items, s16 max, u16 nearl, s16 k)
{
    u16 m0 = stk_mark();
    u16 torp = stk_alloc(2);
    u16 p = PLAYER();
    u16 f = DSW(p + 0x34);
    s16 shotai = DSS(f + 6) == 3 && DSB(f + 0x42) == 1;
    s16 p_lead = DSW(p + 0xDD) == 0;
    s16 side = DSC(p + 0x26);
    s16 lead_near = 0, all_cover = 1, led = 0;
    for (s16 i = 0; i < k; i++) {
        u16 mm = DSW((u16)(nearl + 2 * i));
        if (DSW(mm + 0xDD) == 0) lead_near = 1;
        else if (DSW(mm + 0xDD) == p) {
            led++;
            if (DSW(mm + 0x4A) != 0x1809) all_cover = 0;
        }
    }
    s16 bomb = (s16)mates_have_bombers(p, (u16)PAGE, torp);
    if (!lead_near) PAGE = k > 1 ? 2 : 0;
    s16 is_leader = (s16)plane_in_formation(p);
    s16 n = 0;
    if (SUBMENU) {
        n = targets_list(items, n, bomb == 0);
    } else if (!is_leader || (!bomb && shotai) || PAGE == 0) {
        PAGE = 0;
        if (!bomb) {
            if (side == 0) put(items, &n, 0x33);
            if (DSW(p + 0xDD) != 0) {
                put(items, &n, (u16)(0x34 + shotai));
            } else if (led) {
                if (shotai) put(items, &n, 0x38);
                else { put(items, &n, 0x36); put(items, &n, 0x37); }
                put(items, &n, (u16)((all_cover ? 0x39 : 0x3B) + shotai));
                put(items, &n, 0x3D);
            }
            if (is_leader) n = add_attack_item(items, n, 0);
            if (p_lead && !lead_near) put(items, &n, 0x50);
        } else if (is_leader) {
            n = targets_list(items, n, 0);
        }
    } else if (PAGE < 0) {
        PAGE = -1;
        if (led) put(items, &n, 0x2E);
        if (lead_near) put(items, &n, 0x2F);
        if (targets_exist(bomb)) put(items, &n, 0x30);
    } else if (PAGE == 1) {
        n = targets_list(items, n, bomb == 0);
        if (DSW(torp) && DSB(f + 2) == 3 && DSB(f + 0x43) == 2) {
            u16 t = DSW(f + 0x10);
            if (within_50000(t, p)) {
                if (!(DSB(t + 0x1E) & 0x40)) put(items, &n, 0x43);
                if (!(DSB(t + 0x1E) & 0x80)) put(items, &n, 0x44);
            }
        }
    } else if (PAGE == 2) {
        n = targets_list(items, n, bomb == 0);
        if (!bomb) put(items, &n, 0x50);
    }
    stk_release(m0);
    return n;
}

/* AP 1660:0f5f orders_title -> STR(7) id of the menu title */
s16 orders_title(void)
{
    return DSS((u16)(0x1900 + 2 * PAGE));
}

/* AP 1660:0f6e orders_reset */
void orders_reset(void)
{
    PAGE = -2;
    SUBMENU = 0;
    STEP = 0;
}

/* AP 1660:0f85 order_send(code, index) -> 1 sent / 0 the menu continues */
s16 order_send(s16 code, s16 index)
{
    if (code >= 0x2E && code <= 0x30) {
        PAGE = (s16)(code - 0x2E);
        radio_menu_rebuild();
        if (STEP == 2) { SUBMENU = 1; STEP = 0; }
        return 0;
    }
    if (code >= 0x45 && code <= 0x48) {
        u16 m = stk_mark();
        u16 ctx = stk_alloc(0x18), bear = stk_alloc(2), pos = stk_alloc(12);
        u16 p = PLAYER();
        s16 s = DSB(p + 0x26) == 1;
        DSL(ctx + 0x0E) = 0;
        DSW(ctx + 0x16) = 0;
        u16 r = player_contact_report(DSS((u16)(0x187C + 2 * code)), bear, pos);
        if (r & 1) {
            DSL(ctx + 2) = DSL(pos + 8) - DSL(p + 0x0C);
            DSL(ctx + 6) = DSL(pos + 8);
            DSW(ctx) = DSW(bear);
            radio_queue5((u16)(s + 5), ctx, 1);
        } else if (r & 2) {
            radio_queue5((u16)(s + 7), ctx, 1);
        } else {
            s16 lead = plane_in_formation(p) ? 0 : 1;
            radio_queue5((u16)(DSS(0x5BCC) + lead * 3 + s * 6 + 9), ctx, 1);
            if (DSS(0x5BCC) < 2) DSW(0x5BCC) += 1;
        }
        stk_release(m);
        return 1;
    }
    if (code == 0x31 || code == 0x32) {
        if (STEP == 0) SUBMENU = 1;
        else STEP = 2;
        radio_menu_rebuild();
        return 0;
    }
    if (code == 0x49) {
        lso_request();
        return 1;
    }
    u16 tgt = (code == 0x40 || code == 0x41 || code == 0x42) ? TGT(index) : 0;
    ai_order_execute((u16)code, (u16)PAGE, tgt);
    return 1;
}

/* AP 1660:110f orders_items(items, max) -> n */
s16 orders_items(u16 items, s16 max)
{
    u16 m0 = stk_mark();
    u16 list = stk_alloc(0x40), buf = stk_alloc(0x84);
    u16 p = PLAYER();
    s16 n = 0;
    s16 k = (s16)flight_alive_members(DSW(p + 0x34), list);
    if (k <= 0) { stk_release(m0); return 0; }
    s16 nearc = 0, led = 0;
    u16 leader = 0;
    for (s16 i = 0; i < k; i++) {
        u16 mm = DSW((u16)(list + 2 * i));
        if (mm == p) continue;
        if (ldist2((u16)(mm + 4), (u16)(p + 4)) >= 80000) continue;
        DSW((u16)(list + 2 * nearc)) = mm;
        nearc++;
        if (DSW(mm + 0xDD) == 0) leader = mm;
        else if (DSW(mm + 0xDD) == p) led++;
    }
    u16 h = mates_have_bombers(p, 2, 0);
    if (nearc != 0) {
        if (STEP == 2) {
            if (led) put(items, &n, 0x2E);
            if (leader) put(items, &n, 0x2F);
            if (nearc > 1) put(items, &n, 0x30);
        } else {
            s16 cond = 1;
            if (!SUBMENU) cond = h ? (DSB(DSW(p + 0x34) + 0x3B) & 1) : (s16)spotted_split(buf, p, 0);
            if (SUBMENU || cond) {
                n = orders_list(items, max, list, nearc);
            } else {
                for (s16 c = 0; c < 4; c++) put(items, &n, (u16)(0x45 + c));
                PAGE = 2;
                if (plane_in_formation(p)) {
                    n = add_attack_item(items, n, (s16)mates_have_bombers(p, (u16)PAGE, 0));
                    STEP = 1;
                }
            }
        }
    }
    if (!SUBMENU && lso_can_request()) {
        put(items, &n, 0x49);
        if (PAGE < -1) PAGE = -1;
    }
    stk_release(m0);
    return n;
}

/* ---- 6e5a: the menu box ----------------------------------------------------------------------------- */

/* AP 6e5a:0000 radio_menu_build(n, items) */
void radio_menu_build(s16 n, u16 items)
{
    u16 m = stk_mark();
    u16 tmp = stk_alloc(0x9E);
    DSL(0x3BCF) = 0;
    DSW(0x3BC5) = 0;
    gfx_font_select(DSS(0x3F2E));
    order_text(orders_title(), 0, LINE(0));
    u8 digit = '1';
    for (s16 i = 0; i < n; i++, digit++) {
        u16 line = LINE(i + 1);
        order_text(DSS((u16)(items + 2 * i)), i, tmp);
        memcpy(DSP(line), DSP(0x120A), 4);                      /* "F? " */
        DSB(line + 1) = digit;
        rtl_strcat(line, tmp);
        DSW(0x3BC5) = (u16)imax(DSS(0x3BC5), (s16)(gfx_text_width(line) + 2));
    }
    n++;
    order_text(0x2D, 0, LINE(n));
    n++;
    LINES = n;
    gfx_font_select(DSS(0x3F30));
    DSW(0x3BC5) = (u16)imax(DSS(0x3BC5), (s16)(gfx_text_width(0x11F2) + 0x18));
    DSW(0x3BC5) += 0x0C;
    DSW(0x5244) = DSW(0x3BC5);
    DSW(0x3BC9) = DSW(0x5248) = (u16)((s16)(0x140 - DSS(0x3BC5)) / 2);
    DSW(0x3BC7) = DSW(0x5246) = 0x19;
    DSW(0x3BC3) = DSW(0x5242) = (u16)(gfx_font_height(DSS(0x3F30)) + gfx_font_height(DSS(0x3F2E)) * LINES + 0x10);
    if (!DSB(0x1206)) {
        DSB(0x1206) = 1;
        DSW(0x1207) = 0xFFFE;
    }
    DSB(0x1209) = 0;
    stk_release(m);
}

/* AP 6e5a:01a0 radio_menu_draw(redraw) */
void radio_menu_draw(s16 redraw)
{
    DSW(0x26D6) = DSW(AP_DS_drv_page_front);
    if (!DSB(0x1209) || redraw == 2) {
        DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
        DSL(0x3BCF) = 0;
        DSW(0x3BC9) = DSW(0x5248);
        DSW(0x3BC7) = DSW(0x5246);
        DSW(0x3BC5) = DSW(0x5244);
        DSW(0x3BC3) = DSW(0x5242);
        window_frame_title(far_make(0, 0), far_make(0, 0));
        gfx_font_select(DSS(0x3F30));
        text_engrave(0x11F2, 0xC1, 0xB1, 0xC0, (u16)(DSW(0x3BC9) + 0x0C), (u16)(DSW(0x3BC7) + 3));
        DSW(0x3BC9) += 3;
        DSW(0x3BC5) -= 6;
        DSW(0x3BC7) += (u16)(gfx_font_height(DSS(0x3F30)) + 6);
        DSW(0x3BC3) -= (u16)(gfx_font_height(DSS(0x3F30)) + 0x0D);
        inset_box_item(far_make(0, 0));
        DSW(0x3BC7) += 3;
        DSW(0x3BC9) += 3;
        gfx_font_select(DSS(0x3F2E));
        DSB(0x26C0) = 0xC1;
        DSB(0x26C2) = 1;
        for (s16 i = 0; i < LINES; i++)
            gfx_text(LINE(i), DSS(0x3BC9), (s16)(DSS(0x3BC7) + i * gfx_font_height(DSS(0x3F2E))));
        DSB(0x1209) = 1;
    } else {
        DSB(0x26CD) = 0;
        DSB(0x26CE) = 0;
        u32 save = DSL(0x3586);
        DSL(0x3586) = DSL(0x3576);                                /* fill through the span copy entry */
        gfx_rect(DSS(0x5248), DSS(0x5246), (s16)(DSS(0x5244) + 1), (s16)(DSS(0x5242) + 1));
        DSL(0x3586) = save;
    }
    if (DSS(0x1207) <= -2) return;
    s16 si = DSS(0x1207) == -1 ? (s16)(LINES - 1) : (s16)(DSS(0x1207) + 1);
    s16 y = (s16)(gfx_font_height(DSS(0x3F30)) + DSS(0x5246) + si * gfx_font_height(DSS(0x3F2E)) + 8);
    DSB(0x26CC) = 1;
    DSB(0x26CE) = 0xB9;
    DSB(0x26CD) = 0xBE;
    gfx_rect((s16)(DSS(0x5248) + 5), y, (s16)(DSS(0x5244) - 9), gfx_font_height(DSS(0x3F2E)));
    gfx_text(LINE(si), (s16)(DSS(0x5248) + 6), (s16)(y + 1));
    DSB(0x1209) = 0;
}

/* AP 6e5a:038b */
void radio_menu_reset(void)
{
    DSW(0x523E) = 0;
}

/* AP 6e5a:0396 */
void radio_menu_rebuild(void)
{
    DSW(0x523E) = 1;
}

/* AP 6e5a:03a1 radio_menu_key(cmd) -> the selection (-2 none, -1 Esc) */
s16 radio_menu_key(s16 cmd)
{
    if (DSW(0x523E)) {
        DSW(0x523E) = 0;
        DSW(0x1207) = 0xFFFE;
    } else if (DSS(0x1207) > -2) {
        DSB(0x1206) = 0;
        DSW(AP_DS_redraw_pages) = 2;
        vcr_redraw_all();
        redraw_all();
        DSW(0x1207) = 0xFFFE;
    } else if (cmd == 0x1F) {
        DSW(0x1207) = 0xFFFF;
    } else if (cmd >= 0x18 && cmd <= 0x1E && (s16)(cmd - 0x18) < (s16)(LINES - 2)) {
        DSW(0x1207) = (u16)(cmd - 0x18);
    }
    return DSS(0x1207);
}
