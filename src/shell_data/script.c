/* shell_data: miss\script.c (PS 3164) — mission building helpers, the SCRIPT.DAT writer and the
 * mission file -> memory loader; miss\tables.c (3555). shell_data.md §5-9 (read against the
 * disassembly: argument orders, signedness and the quirks below are the original's). */
#include "shell_data/sd_priv.h"
#include "shell_ui/shell_ui.h"

/* flight record fields */
enum { FL_SVC = 0, FL_FORM = 1, FL_ROLE = 2, FL_LOAD = 3, FL_B04 = 4, FL_HDG = 5, FL_CARR = 6, FL_B07 = 7,
       FL_TK = 8, FL_B09 = 9, FL_SKILL = 0xA, FL_ACE = 0xE, FL_SQ = 0x16, FL_TYPE = 0x18, FL_PLANES = 0x1A,
       FL_NWP = 0x1C, FL_HOME = 0x1E, FL_TGT = 0x20, FL_WP = 0x22 };
enum { CV_SVC = 0, CV_KIND = 1, CV_NAME = 2, CV_CALL = 3, CV_TYPES = 4, CV_REC = 0xC, CV_NSHIP = 0xE,
       CV_NWP = 0x10, CV_WP = 0x12 };
enum { PL_CLS = 0, PL_KIND = 1, PL_SVC = 2, PL_FLAG = 3, PL_REC = 4, PL_POS = 6 };

#define DATE    DSW(PS_DS_mission_date)
#define NFL(s)  DSW((u16)(PS_DS_flight_count + (s) * 2))
#define AREA    DSB(MAREA)
#define MT      DSC(MTYPE)
#define KIND    DSC(MKIND)

/* loader tables (DS:61B6..61D2) */
#define LT_T3   0x61B6
#define LT_GRP  0x61B8
#define LT_WP   0x61BA
#define LT_PL   0x61BC
#define LT_SH   0x61BE
#define LT_AC   0x61C0
#define LN_T3   0x61C2
#define LN_WP   0x61C4
#define LN_GRP  0x61C6
#define LN_PL   0x61C8
#define LN_SH   0x61CA
#define LN_AC   0x61CC

static bool lt32(s32 d, s32 r) { return d < r; }

/* the r1/r2 ranges of the flight type: ((A*P/100)*21128, ((A+B)*P/100)*21128) */
static void ranges(s16 type, s32 pct, s32 *r1, s32 *r2)
{
    s32 a = ac_range(type);
    s32 ab = (s32)((u32)a + (u32)ac_range_ext(type));
    a = (s32)((u32)a * (u32)pct) / 100;
    ab = (s32)((u32)ab * (u32)pct) / 100;
    *r1 = (s32)((u32)a * 21128u);
    *r2 = (s32)((u32)ab * 21128u);
}

static bool pos_valid(u16 p) { return !(DSSL(p) == -1 || DSSL(p + 4) == -1); }

/* "<base>.<area>": itoa(area, DS:411A) then make_name(base, -1, DS:411A) */
static void load_world(u16 base_ds, u16 area)
{
    rtl_itoa((s16)area, 0x411A, 10);
    wd_load_database(make_name(base_ds, -1, 0x411A));
}

/* ================================================================================================ */

/* PS 3164:0005 mission_free */
void ps_mission_free_3164(void)
{
    for (s16 s = 0; s < 3; s++)
        for (s16 i = 0; i < (s16)NFL(s); i++) {
            u16 f = FLT(s, i);
            if (DSW(f + FL_NWP) != 0) {
                xfree(DSW(f + FL_WP));
                DSW(f + FL_WP) = 0;
                DSW(f + FL_NWP) = 0;
            }
        }
    for (s16 c = 0; c < (s16)DSW(PS_DS_convoy_count); c++) {
        u16 v = CONV(c);
        if (DSW(v + CV_NWP) != 0) {
            xfree(DSW(v + CV_WP));
            DSW(v + CV_WP) = 0;
            DSW(v + CV_NWP) = 0;
        }
    }
}

/* PS 3164:00aa target_class(kind) */
s8 ps_target_class(s8 kind)
{
    if (kind <= 1) return 0;
    if (kind < 7) return 1;
    if (kind == 7) return 2;
    if (kind <= 11) return 3;
    return (s8)0xFF;
}

/* PS 3164:00de pick_role(side, mode, service, n) */
static s8 pick_role(s8 side, s8 mode, s8 service, s8 n)
{
    s8 role = (s8)0xFF;
    u8 area = AREA;
    u16 date = DATE;
    if (mode == 2) {
        s8 k = rng_pick_weighted(0x224A, 2);                 /* {20, 80} */
        if (k == 0) mode = 0;
        else if (k == 1) mode = 1;
    }
    if (mode == 0) return 0;
    s16 a = missinf_find((s8)area);
    s8 c = mi_target_kind(a, service, date);
    u16 f = FLT(side, n);
    if (MT == 9 || c == 1) {
        DSB(f + FL_TK) = 1;
        role = mi_role_land(a, service, date);
        if (KIND == 0) while (role == 0) role = mi_role_land(a, service, date);
    } else if (MT == 8 || c == 0) {
        DSB(f + FL_TK) = 2;
        role = mi_role_ships(a, service, date);
        if (KIND == 0) while (role == 0) role = mi_role_land(a, service, date);   /* 03a2, as coded */
    }
    return role;
}

/* PS 3164:0206 reuse_type(type, slot, n, side) */
static s8 reuse_type(s8 type, s8 slot, s8 n, s8 side)
{
    if (side == 1 || (MT == 7 && side == 2)) slot = (s8)(slot + 3);
    u16 pool = (u16)(PS_DS_type_pools + (s16)MT * 12 + (s16)slot * 2);
    if (DSW(pool) == 0 && DSC(PS_DS_used_type_count) < 7) {
        u16 f = FLT(side, n);
        for (s16 i = 0; i < 7; i++) {
            s8 t = DSC(PS_DS_used_types + i);
            if (ac_has_service(t, DSC(f + FL_SVC)) && ac_class(t) == DSC(f + FL_ROLE)) return t;
        }
        return (s8)0xFF;
    }
    s16 cnt = (s16)DSC(PS_DS_used_type_count);
    for (s16 i = 0; i <= cnt; i++)
        if (ac_selfid(DSC(PS_DS_used_types + i)) == type) return type;
    DSW(pool)--;
    DSB(PS_DS_used_types + cnt) = (u8)type;
    DSB(PS_DS_used_type_count)++;
    return type;
}

/* PS 3164:030b assign_ace(planes, side, n) -> aces assigned (0/1) */
static s16 assign_ace(s16 planes, s8 side, s8 n)
{
    u16 f = FLT(side, n);
    s16 assigned = 0;
    s16 sq = (s16)DSW(f + FL_SQ);
    u8 area = AREA;
    u16 date = DATE;
    s16 thr = (s16)(planes * 100 / 6);
    s16 nace = (s16)(s8)ace_count();
    u16 list = xmalloc((u16)(nace * 2));
    s16 cnt = 0;
    for (s16 a = 0; a < nace; a++) {
        if (!ace_valid(a)) continue;
        bool add = false;
        if (sq != -1) {
            add = ace_squadron(a, date) == sq;
        } else {
            if (DSC(f + FL_SVC) != ace_service(a)) continue;
            if (ace_status(a, date) == 0) {
                s16 asq = ace_squadron(a, date);
                if (squad_status(asq, date) == 0 && (s16)DSW(f + FL_TYPE) == squad_aircraft(asq, date) &&
                    station_area(squad_station(asq, date)) == area) add = true;
            }
            if (!add && ace_status(a, date) == 1) {
                if ((s16)DSW(f + FL_TYPE) == ace_posting_aircraft(a, date) &&
                    station_area(ace_posting_station(a, date)) == area) add = true;
            }
        }
        if (add) { DSW(list + cnt * 2) = (u16)a; cnt++; }
    }
    for (s16 i = 0; i < cnt; i++)
        for (s16 j = 0; j < cnt; j++) {
            if ((s16)DSW(list + j * 2) == -1) continue;
            if (ace_link((s16)DSW(list + j * 2)) == (s16)DSW(list + i * 2)) DSW(list + i * 2) = 0xFFFF;
        }
    s16 n0 = cnt;
    for (s16 i = 0; i < n0; i++) {
        if ((s16)DSW(list + i * 2) != -1) continue;
        u16 t = DSW(list + i * 2);                          /* reads one past the end (quirk) */
        DSW(list + i * 2) = DSW(list + (i + 1) * 2);
        DSW(list + (i + 1) * 2) = t;
        cnt--;
    }
    if (cnt > 0) {
        s16 r = rng_below(100);
        s16 m = cnt < planes ? cnt : planes;
        for (s16 i = 0; i < m && assigned == 0; i++) {
            if (r > thr) continue;
            u16 used = (u16)(DSW(PS_DS_ace_used) + DSW(list + i * 2));
            if (DSC(used) != 0) continue;
            DSW(f + FL_ACE + i * 2) = DSW(list + i * 2);   /* slot i (quirk: the skill loop marks plane 0) */
            DSB(used) = 1;
            assigned++;
        }
    }
    xfree(list);
    return assigned;
}

/* PS 3164:05f9 single_open(block, int *n) -> FILE* of the chosen SINGLE*.DAT */
static u16 single_open(s8 block, u16 n_ds)
{
    u16 m = stk_mark();
    u16 fp9 = stk_alloc(18), cnt = stk_alloc(18);
    open_module_files(fp9, 0x225F, 0x2266);                 /* "single" "dat" */
    s16 nonempty = 0;
    for (s16 i = 0; i < 9; i++) {
        u16 f = DSW(fp9 + i * 2);
        if (!f) { DSW(cnt + i * 2) = 0; continue; }
        for (s16 b = 0; b < (s16)block; b++) {
            if (my_fread((u16)(cnt + i * 2), 2, 1, f) == 0) sd_fatal(FS_SCRIPT, 0x190);
            if (my_fseek(f, (s32)(u32)(u16)(DSW(cnt + i * 2) * 6), 1) != 0) sd_fatal(FS_SCRIPT, 0x193);
        }
        my_fread((u16)(cnt + i * 2), 2, 1, f);
        if (DSW(cnt + i * 2) != 0) nonempty++;
    }
    if (nonempty == 0) sd_fatal(FS_SCRIPT, 0x19F);
    s16 k = rng_below(nonempty);
    s16 si = 0;
    for (;;) {
        if (k != 0) { if (DSW(cnt + si * 2) != 0) k--; si++; continue; }
        if (DSW(cnt + si * 2) == 0) { si++; continue; }
        break;
    }
    for (s16 i = 0; i < 9; i++)
        if (DSW(fp9 + i * 2) && i != si) my_fclose(DSW(fp9 + i * 2));
    module_set_active((u8)si);
    DSW(n_ds) = DSW(cnt + si * 2);
    u16 r = DSW(fp9 + si * 2);
    stk_release(m);
    return r;
}

/* PS 3164:07ef single_pick_context(service) — sets DS:62DB squadron and DS:6277 date */
static void single_pick_context(s8 service)
{
    u16 m = stk_mark();
    u16 n = stk_alloc(2), rec = stk_alloc(6);
    u16 f = single_open(service, n);
    if ((s16)DSW(n) > 0x24) sd_fatal(FS_SCRIPT, 0x1C3);
    s16 r = rng_below(4);
    for (;;) {
        my_fread(rec, 6, 1, f);
        s16 old = (s16)DSW(n);
        DSW(n)--;
        if (old == 0) break;
        if ((s16)MT != (s16)DSW(rec)) continue;
        s16 o = r--;
        if (o == 0) break;
    }
    if (!(r == -1 && (s16)MT == (s16)DSW(rec))) sd_fatal(FS_SCRIPT, 0x1CB);
    DSW(FLT(0, 0) + FL_SQ) = DSW(rec + 2);
    DATE = DSW(rec + 4);
    my_fclose(f);
    stk_release(m);
}

/* PS 3164:08b3 mission_player_slot */
static void mission_player_slot(void)
{
    if (MT == 0) { DSB(FLT(0, 0) + FL_SKILL) = 4; return; }
    ranks_load();
    s8 pos = 0;          /* PORT: uninitialised for kinds other than 0/1 (the builders only use 0/1) */
    if (KIND == 0) pos = 3;
    else if (KIND == 1) pos = (s8)rank_extra(DSC(0x4777));
    u16 f = FLT(0, 0);
    if ((s16)pos + 1 > (s16)DSW(f + FL_PLANES)) DSW(f + FL_PLANES)++;
    for (s16 i = 3; i >= 0; i--) {
        if ((s16)pos == i) {
            DSW(f + FL_ACE + i * 2) = 0xFFFF;
            DSB(f + FL_SKILL + i) = 4;
            break;
        }
        DSW(f + FL_ACE + i * 2) = DSW(f + FL_ACE + (i - 1) * 2);    /* i = 0 reads +0C / +09 */
        DSB(f + FL_SKILL + i) = DSB(f + FL_SKILL + i - 1);
    }
    ranks_free();
}

/* PS 3164:0944 career_pick_mission — mission type, or the MISS number of a historical mission */
s8 career_pick_mission(void)
{
    s8 m = (s8)squad_pick_mission_type((s16)DSW(0x478E), DSW(0x477B), (s16)DSW(0x477D));
    if ((u8)m == 0xFF) {
        m = (s8)squad_mission_param((s16)DSW(0x478E), DSW(0x477B), (s16)DSW(0x477D));
        ps_mission_load(make_name(0x226A, (s16)m, 0x226F), 1);     /* "MISS" <n> "dat" */
        mission_write_script();
        ps_mission_free_1f06();
        DSB(MKIND) = 2;
    } else DSB(MKIND) = 1;
    return m;
}

/* PS 3164:09bd mission_reset */
void mission_reset(void)
{
    rtl_memset(PS_DS_flight_count, 0, 0x2BC);
    rtl_memset(PS_DS_used_types, 0, 2);
    for (s16 i = 0; i < 11; i++)
        rtl_memcpy((u16)(PS_DS_type_pools + i * 12), (u16)(PS_DS_tab_pools + i * 12), 12);
    DSB(PS_DS_used_type_count) = 0;
    DSB(HDR + 6) = 0;
    DSB(MAREA) = 0xFF;
}

/* PS 3164:0a27 mission_build(type, service, kind) */
void mission_build(s8 type, s8 service, s8 kind)
{
    DSW(PS_DS_ace_used) = xmalloc((u16)(s16)(s8)ace_count());
    rtl_memset(DSW(PS_DS_ace_used), 0, (u16)(s16)(s8)ace_count());
    switch (type) {
    case 0: mission_dace(service, kind); break;
    case 1: mission_dsqd(service, kind); break;
    case 2: mission_cap(service, kind); break;
    case 3: mission_fswp(service, kind); break;
    case 4: mission_scrm(service, kind); break;
    case 5: break;
    case 6: mission_esct(service, kind); break;
    case 7: mission_intr(service, kind); break;
    case 8: mission_anti(service, kind); break;
    case 9: mission_grnd(service, kind); break;
    default: sd_fatal(FS_SCRIPT, 0x28D); break;
    }
    DSW(HDR + 8) = 50;
    mission_player_slot();
    xfree(DSW(PS_DS_ace_used));
}

/* PS 3164:0b49 mission_setup(service) — RSEED, context draws, reseed, wind, area */
void mission_setup(s8 service)
{
    s16 si = 0;
    if (KIND == 0) {
        DSW(PS_DS_mission_rseed) = (s16)DSW(PS_DS_rand_seed_opt) > 0 ? DSW(PS_DS_rand_seed_opt) : rng_count();
        single_pick_context(service);
        si = (s16)DSW(FLT(0, 0) + FL_SQ);
        DSW(FLT(1, 0) + FL_SQ) = 0xFFFF;
        DSW(FLT(2, 0) + FL_SQ) = 0xFFFF;
        DSB(HDR + 0xE) = (u8)rng_pick_weighted((u16)(PS_DS_tab_condition + (s16)MT * 18), 9);
        if (DSC(HDR + 0xE) > 4 && DSC(HDR + 0xE) != 8) DSB(HDR + 0xE) = 1;
    } else if (KIND == 1) {
        DSW(PS_DS_mission_rseed) = rng_count();
        DATE = DSW(0x477F);
        si = (s16)DSW(0x478E);
        DSW(FLT(0, 0) + FL_SQ) = (u16)si;
        DSW(FLT(1, 0) + FL_SQ) = 0xFFFF;
        DSW(FLT(2, 0) + FL_SQ) = 0xFFFF;
        DSB(HDR + 0xE) = (u8)rng_pick_weighted((u16)(PS_DS_tab_condition + (s16)MT * 18), 9);
    }
    ps_rng_seed(DSW(PS_DS_mission_rseed));
    u16 m = stk_mark();
    u16 d = stk_alloc(4);
    ps_date_split(DATE, ds_ptr(d));
    DSB(HDR + 4) = (u8)rng_pick_weighted((u16)(PS_DS_tab_wind + (s16)DSC(d) * 8), 4);
    DSB(MAREA) = station_area(squad_station(si, DATE));
    stk_release(m);
}

/* PS 3164:0c50 enemy_service(service) */
s8 enemy_service(s8 service)
{
    s16 a = missinf_find((s8)AREA);
    s8 side = (s8)service_side((u16)(s16)service);
    return mi_enemy_service(a, side, DATE);
}

/* PS 3164:0c89 add_flight(service, side, mode) */
void add_flight(s8 service, s8 side, s8 mode)
{
    s16 area_idx = -1;
    s8 n = (s8)DSB(PS_DS_flight_count + side * 2);
    u16 f = FLT(side, n);
    s16 sq = (s16)DSW(f + FL_SQ);
    s8 mtype = MT;
    u16 date = DATE;
    DSB(f + FL_SVC) = (u8)service;
    s16 type;
    s8 role;
    if (sq > -1) {
        type = squad_aircraft(sq, date);
        role = ac_class(type);
    } else {
        role = pick_role(side, mode, service, n);
        s16 cnt = wf_count();
        for (s16 i = 0; i < cnt; i++)
            if ((u8)wf_area(i) == AREA) { area_idx = i; break; }
        type = (s16)wf_aircraft(area_idx, service, role, date);
    }
    type = (s16)reuse_type((s8)type, mode, n, side);
    f = FLT(side, n);
    DSB(f + FL_ROLE) = (u8)role;
    DSW(f + FL_TYPE) = (u16)type;
    s8 planes = (s8)flight_planes(service, role, date);
    DSW(f + FL_PLANES) = (u16)(s16)planes;
    s8 cls = sq > -1 ? (s8)squad_quality(sq) : wf_quality(area_idx, service, date);
    s16 aces = role == 0 ? assign_ace((s16)planes, side, n) : 0;
    for (s16 i = 0; i < (s16)planes; i++) {
        if (aces) {
            DSB(f + FL_SKILL + i) = 0xFF;
            aces--;
        } else {
            DSB(f + FL_SKILL + i) = (u8)rng_pick_weighted((u16)(PS_DS_tab_skill + (s16)cls * 8), 4);
        }
    }
    DSB(f + FL_FORM) = flight_formation(service, role, date);
    DSB(f + FL_LOAD) = (u8)ord_pick(mtype, (s8)type, 0, side);
    DSB(f + FL_B07) = (side == 0 && n == 0) ? 0 : 3;
    DSB(f + FL_B04) = 1;
    DSW(PS_DS_flight_count + side * 2) = (u16)((s16)n + 1);
}

/* PS 3164:0f39 add_escort_flights(service, side, mode) — pick({30,50,20}) flights */
void add_escort_flights(s8 service, s8 side, s8 mode)
{
    u16 m = stk_mark();
    u16 w = stk_alloc(6);
    for (int k = 0; k < 6; k++) DSB(w + k) = DSB(0x224E + k);
    s8 k = rng_pick_weighted(w, 3);
    stk_release(m);
    for (; k > 0; k--) {
        s8 n = (s8)DSB(PS_DS_flight_count + side * 2);
        DSW(FLT(side, n) + FL_SQ) = 0xFFFF;
        add_flight(service, side, mode);
    }
}

/* PS 3164:0fb2 pick_convoy(side, ckind, service, quality, from, out) */
u16 pick_convoy(s8 side, s8 ckind, s8 service, s8 quality, u16 from_ds, u16 out_ds)
{
    s16 c = -1;
    u8 found = 0;
    u8 tried[3];
    for (int k = 0; k < 3; k++) tried[k] = DSB(0x2254 + k);
    ship_open();
    u16 date = DATE;
    u8 area = AREA;
    s16 ci = (s16)DSW(PS_DS_convoy_count);
    s8 n = (s8)(DSB(PS_DS_flight_count + side * 2) - 1);
    for (int k = 0; k < 12; k += 2) DSW(out_ds + k) = 0xFFFF;
    s16 nships = ship_count();
    s32 r1, r2;
    ranges((s16)DSW(FLT(side, n) + FL_TYPE), 40, &r1, &r2);
    s8 sidegroup = (s8)service_side((u16)(s16)service);
    for (s16 i = 0; i < nships; i++) {
        if ((u16)area == (u16)(u8)ship_area(i) && ship_kind(i) == ckind) { c = i; break; }
        c = -1;
    }
    s8 q = ship_pick_class(c, quality, date);
    load_world(0x2273, area);                                 /* "SHIPPATH" */
    s16 nrec = wd_count();
    u16 list = xmalloc((u16)(nrec * 2));
    s16 m = 0;
    while (!found) {
        tried[(u8)q] = 1;
        m = 0;
        for (s16 i = 0; i < nrec; i++) {
            if (wd_kind(i, date, 0, 0) != (s16)q) continue;
            if (!wd_side_matches(i, date, sidegroup, 0)) continue;
            wd_position_mid(i, date, out_ds, 0);
            if (!pos_valid(out_ds)) continue;
            s32 d = dist(out_ds, from_ds);
            if (!(lt32(d, r1) || lt32(d, r2))) continue;
            found = 1;
            DSW(list + m * 2) = (u16)wd_record_id(i);
            m++;
        }
        if (!found) {
            q = (s8)0xFF;
            for (s8 k = 0; k < 3; k++) if (!tried[k]) q = k;
            if ((u8)q == 0xFF) {
                wd_close();
                xfree(list);
                for (int k = 0; k < 8; k += 2) DSW(out_ds + k) = 0xFFFF;   /* x and z only */
                return out_ds;
            }
        }
    }
    u16 mk = stk_mark();
    u16 types = stk_alloc(4), w = stk_alloc(6), rec = stk_alloc(14);
    ship_convoys(c, q, sidegroup, date, types);
    ship_close();
    convoy_open();
    for (s16 k = 0; k < 3; k++) DSW(w + k * 2) = (u16)(s16)(s8)convoy_weight((s16)DSC(types + k));
    s8 t = rng_pick_weighted(w, 3);
    convoy_get(ds_ptr(rec), (s16)DSC(types + t));
    u16 v = CONV(ci);
    DSB(v + CV_SVC) = (u8)service;
    DSB(v + CV_KIND) = DSB(rec + 0xB);
    DSB(v + CV_NAME) = DSB(rec + 1);
    DSW(v + CV_NSHIP) = (u16)(s16)DSC(rec + 2);
    DSW(v + CV_NWP) = 1;
    DSW(v + CV_WP) = xmalloc(12);
    for (s16 k = 0; k < (s16)DSC(rec + 2); k++) DSB(v + CV_TYPES + k) = DSB(rec + 3 + k);
    convoy_close();
    s16 p = rng_below((s16)(m - 1));
    DSW(v + CV_REC) = DSW(list + p * 2);
    wd_position_mid((s16)DSW(list + p * 2), date, 0x6196, 1);
    rtl_memcpy(DSW(v + CV_WP), 0x6196, 12);
    rtl_memcpy(out_ds, 0x6196, 12);
    if (dist(out_ds, from_ds) > r1) {
        u16 f = FLT(side, n);
        DSB(f + FL_LOAD) = (u8)ord_pick(MT, (s8)DSW(f + FL_TYPE), 1, side);
    }
    stk_release(mk);
    wd_close();
    xfree(list);
    DSW(PS_DS_convoy_count)++;
    return out_ds;
}

/* PS 3164:14bc pick_ground_target(side, service, from, out) */
u16 pick_ground_target(s8 side, s8 service, u16 from_ds, u16 out_ds)
{
    s16 total = 0;
    s8 n = (s8)(DSB(PS_DS_flight_count + side * 2) - 1);
    u16 date = DATE;
    u8 area = AREA;
    for (int k = 0; k < 12; k += 2) DSW(out_ds + k) = 0xFFFF;
    u16 f = FLT(side, n);
    s32 r1, r2;
    ranges((s16)DSW(f + FL_TYPE), 45, &r1, &r2);
    s8 sidegroup = (s8)service_side((u16)(s16)service);
    load_world(0x227C, area);                                 /* "TARGET" */
    u16 mk = stk_mark();
    u16 wraw = stk_alloc(8), sum = stk_alloc(8), pct = stk_alloc(8);
    s16 a = missinf_find((s8)area);
    mi_ground_weights(a, date, wraw);
    s16 nrec = wd_count();
    u16 list = xmalloc((u16)(nrec * 2));
    rtl_memset(sum, 0, 8);
    s16 m = 0;
    for (s16 i = 0; i < nrec; i++) {
        if (!wd_side_matches(i, date, sidegroup, 0)) continue;
        s16 kind = wd_kind(i, date, 0, 0);
        if (kind <= -1) continue;
        wd_position(i, date, out_ds, 0);
        if (!pos_valid(out_ds)) continue;
        s32 d = dist(out_ds, from_ds);
        s8 c = ps_target_class((s8)kind);
        s16 add;
        if (d < 0x305BB0) {
            if (c == 3 && DSC(f + FL_ROLE) != 3) continue;
            add = (s16)((s16)DSC(wraw + c) * 2);
        } else {
            if (!(lt32(d, r1) || lt32(d, r2))) continue;
            if (c == 3 && DSC(f + FL_ROLE) != 3) continue;
            add = (s16)DSC(wraw + c);
        }
        DSW(sum + c * 2) = (u16)(DSW(sum + c * 2) + add);
        DSW(list + m * 2) = (u16)wd_record_id(i);
        m++;
    }
    if (m == 0) {
        xfree(list);
        wd_close();
        for (int k = 0; k < 8; k += 2) DSW(out_ds + k) = 0xFFFF;         /* x and z only */
        stk_release(mk);
        return out_ds;
    }
    for (s16 k = 0; k < 4; k++) total = (s16)(total + (s16)DSW(sum + k * 2));
    for (s16 k = 0; k < 4; k++) {
        s32 num = (s32)(s16)DSW(sum + k * 2) * 100;
        /* PORT: a zero total divides by zero in LDIV@ (never with the shipped weights) */
        DSW(pct + k * 2) = total ? (u16)(num / (s32)total) : 0;
    }
    s8 cls = rng_pick_weighted(pct, 4);
    s16 m2 = 0;
    for (s16 i = 0; i < m; i++) {
        if (ps_target_class((s8)wd_kind((s16)DSW(list + i * 2), date, 0, 1)) == cls) {
            DSW(list + m2 * 2) = DSW(list + i * 2);
            m2++;
        }
    }
    s16 pick = (s16)DSW(list + rng_below((s16)(m2 - 1)) * 2);
    xfree(list);
    wd_position(pick, date, out_ds, 1);
    s16 kind = wd_kind(pick, date, 0, 1);
    wd_close();
    s16 cx = (s16)DSW(PS_DS_place_count);
    for (s16 i = 0; i < cx; i++) {
        u16 p = PLACE(i);
        if (DSSL(p + PL_POS) == DSSL(out_ds) && DSSL(p + PL_POS + 4) == DSSL(out_ds + 4)) {
            DSW(f + FL_TGT) = (u16)i;
            DSB(f + FL_TK) = 1;
            cx = -1;
            break;
        }
    }
    if (cx > -1) {
        DSW(f + FL_TGT) = (u16)cx;
        DSB(f + FL_TK) = 1;
        u16 p = PLACE(cx);
        DSB(p + PL_CLS) = (u8)cls;
        DSB(p + PL_KIND) = (u8)kind;
        DSB(p + PL_SVC) = (u8)service;
        DSB(p + PL_FLAG) = cls == 0 ? 1 : 0;
        DSW(p + PL_REC) = (u16)pick;
        rtl_memcpy((u16)(p + PL_POS), out_ds, 12);
        DSW(PS_DS_place_count)++;
    }
    if (dist(out_ds, from_ds) > r1)
        DSB(f + FL_LOAD) = (u8)ord_pick(MT, (s8)DSW(f + FL_TYPE), 1, side);
    stk_release(mk);
    return out_ds;
}

/* PS 3164:1a38 pick_base(side, station, ref_side, out) — n and the range type come from ref_side's
 * last flight when a reference side is given (as coded) */
u16 pick_base(s8 side, s16 station, s8 ref_side, u16 out_ds)
{
    s16 j = 0;
    u16 mk = stk_mark();
    u16 recv = stk_alloc(2), areav = stk_alloc(2), ref = stk_alloc(12), name = stk_alloc(0x34);
    DSW(recv) = 0;
    s8 n = (s8)(DSB(PS_DS_flight_count + (ref_side == -1 ? side : ref_side) * 2) - 1);
    s16 type = (s16)DSW(FLT(ref_side == -1 ? side : ref_side, n) + FL_TYPE);
    u16 f = FLT(side, n);
    s8 squad = (s8)DSB(f + FL_SQ);
    s8 service = DSC(f + FL_SVC);
    u16 date = DATE;
    s8 sidegroup = (s8)service_side((u16)(s16)service);
    s32 r1 = 0, r2 = 0;
    if (station != -1) {
        station_get(station, recv, areav);
        DSB(f + FL_CARR) = station_is_carrier(station);
        rtl_strcpy(name, DSB(f + FL_CARR) ? 0x22A1 : 0x22A9);
        load_world(name, DSW(areav));
        if (wd_side_matches((s16)DSW(recv), date, sidegroup, 1)) wd_position((s16)DSW(recv), date, out_ds, 1);
    } else if (squad > -1) {
        s16 st = squad_station((s16)squad, date);
        station_get(st, recv, areav);
        DSB(f + FL_CARR) = station_is_carrier(st);
        rtl_strcpy(name, DSB(f + FL_CARR) ? 0x2283 : 0x228B);
        load_world(name, DSW(areav));
        if (wd_side_matches((s16)DSW(recv), date, sidegroup, 1)) wd_position((s16)DSW(recv), date, out_ds, 1);
    } else {
        s16 m = 0;
        u16 area = (u16)(s16)DSC(MAREA);
        if (ref_side != -1) {
            u16 rf = FLT(ref_side, 0);
            s16 home = (s16)DSW(rf + FL_HOME);
            if (DSB(rf + FL_CARR)) rtl_memcpy(ref, DSW(CONV(home) + CV_WP), 12);
            else rtl_memcpy(ref, (u16)(PLACE(home) + PL_POS), 12);
        }
        ranges(type, 40, &r1, &r2);
        load_world(0x2292, area);                             /* "TARGET" */
        s16 nrec = wd_count();
        u16 list = xmalloc((u16)(nrec * 2));
        s16 rec = 0;
        while (rec == 0) {
            rec = wd_find_for_service(date, service, j++);
            if (rec > -1 && !wd_side_matches(rec, date, sidegroup, 1)) rec = -2;
            if (ref_side == -1 && rec > -1) {
                s16 k = wd_kind(rec, date, 1, 1);
                if (!(k > 1 || k == -1)) { DSW(list + m * 2) = (u16)rec; m++; }
                rec = -2;
            }
            if (ref_side > -1 && rec > -1) {
                wd_position(rec, date, out_ds, 1);
                s32 d = dist(ref, out_ds);
                if (lt32(d, r1) || lt32(d, r2)) { DSW(list + m * 2) = (u16)rec; m++; }
                rec = 0;
            } else if (rec == -2) rec = 0;
        }
        rec = m > 0 ? (s16)DSW(list + rng_below(m) * 2) : -1;
        if (rec == -1) {
            j = 0;
            rec = 0;
            load_world(0x2299, area);                         /* "CARRIER" */
            while (rec == 0) {
                rec = wd_find_for_service(date, service, j++);
                if (rec > -1 && !wd_side_matches(rec, date, sidegroup, 1)) rec = -1;
                if (ref_side > -1 && rec > -1) {
                    wd_position(rec, date, out_ds, 1);
                    s32 d = dist(ref, out_ds);
                    if (d > r1 && d > r2) rec = 0;
                }
            }
            if (rec != -1) DSB(f + FL_CARR) = 1;
        }
        if (ref_side > -1 && rec <= 0) {
            DSW(out_ds) = DSW(out_ds + 2) = 0xFFFF;
            xfree(list);
            stk_release(mk);
            return out_ds;                                    /* (the world database stays loaded) */
        }
        wd_position(rec, date, out_ds, 1);
        xfree(list);
        DSW(recv) = (u16)rec;
    }
    s16 rec = (s16)DSW(recv);
    if (DSB(f + FL_CARR)) {
        s16 ci = (s16)DSW(PS_DS_convoy_count);
        s16 kind = wd_kind(rec, date, 0, 1);
        u16 v = CONV(ci);
        if (side == 0) {
            u16 cf = my_fopen(make_name(0x22B0, -1, 0x22B9), 0x22BD);     /* CARRFORM.DAT */
            if (!cf) sd_fatal(FS_SCRIPT, 0x613);
            u16 cnt = stk_alloc(2), buf = stk_alloc(0x40);
            my_fread(cnt, 2, 1, cf);
            my_fread(buf, 10, (s16)DSW(cnt), cf);
            my_fclose(cf);
            DSW(f + FL_HOME) = (u16)ci;
            DSB(v + CV_SVC) = DSB(f + FL_SVC);
            DSB(v + CV_KIND) = 0x18;
            DSB(v + CV_NAME) = 0;
            DSB(v + CV_CALL) = 0;
            s16 row = -1;
            for (s16 i = 0; i < (s16)DSW(cnt); i++)
                if ((s16)(s8)service_side((u16)(s16)DSC(v + CV_SVC)) == (s16)DSW(buf + i * 10)) { row = i; break; }
            for (s16 i = 0; i < 4; i++) {
                /* PORT: row -1 reads the frame below the local array in the original; the port reads
                 * the bytes before it on the emulated stack */
                s16 t = (s16)DSW((u16)(buf + row * 10 + 2 + i * 2));
                DSB(v + CV_TYPES + i) = t == -1 ? (u8)kind : (u8)t;
            }
            DSW(v + CV_REC) = (u16)rec;
            DSW(v + CV_NSHIP) = 4;
        } else {
            DSW(f + FL_HOME) = (u16)ci;
            DSB(v + CV_SVC) = DSB(f + FL_SVC);
            DSB(v + CV_KIND) = 0x18;
            DSB(v + CV_NAME) = 0;
            DSB(v + CV_CALL) = 0;
            DSB(v + CV_TYPES) = (u8)kind;
            DSW(v + CV_REC) = (u16)rec;
            DSW(v + CV_NSHIP) = 1;
        }
        DSW(v + CV_NWP) = 1;
        DSW(v + CV_WP) = xmalloc(12);
        rtl_memcpy(DSW(v + CV_WP), out_ds, 12);
        DSW(PS_DS_convoy_count)++;
    } else {
        s16 pc = (s16)DSW(PS_DS_place_count);
        s16 kind = wd_kind(rec, date, 0, 1);
        for (s16 i = 0; i < pc; i++) {
            u16 p = PLACE(i);
            if (DSSL(p + PL_POS) == DSSL(out_ds) && DSSL(p + PL_POS + 4) == DSSL(out_ds + 4)) {
                DSW(f + FL_HOME) = (u16)i;
                DSB(p + PL_CLS) = 0;
                pc = -1;
                break;
            }
        }
        if (pc > -1) {
            u16 p = PLACE(pc);
            DSB(p + PL_KIND) = kind == -1 ? 0x0D : (u8)kind;
            DSB(p + PL_SVC) = DSB(f + FL_SVC);
            if ((side == 0 && n == 0) || (MT == 3 && ref_side > -1)) DSB(p + PL_FLAG) = 1;
            DSW(p + PL_REC) = (u16)rec;
            rtl_memcpy((u16)(p + PL_POS), out_ds, 12);
            DSW(PS_DS_place_count)++;
        }
    }
    wd_close();
    if (ref_side > -1 && dist(ref, out_ds) > r1)
        DSB(f + FL_LOAD) = (u8)ord_pick(MT, (s8)type, 1, side);
    stk_release(mk);
    return out_ds;
}

/* PS 3164:2387 pilot_code(side, flight, plane) */
static u8 pilot_code(s16 side, s16 flight, s16 plane)
{
    u16 f = FLT(side, flight);
    s8 sk = DSC(f + FL_SKILL + plane);
    if (sk == 4) return 0x2C;
    if (sk > -1) return (u8)((u8)(DSB(f + FL_SVC) << 2) + (u8)sk);
    return (u8)(DSB(f + FL_ACE + plane * 2) + 0x2D);
}

static void wr_u16w(u16 f, u16 tmp, u16 v) { DSW(tmp) = v; my_fwrite(tmp, 2, 1, f); }

/* PS 3164:23e3 script_write(resume) — SCRIPT.DAT (shell_data.md §8) */
void script_write(s8 resume)
{
    if (KIND == 2 && resume) { DSB(MKIND) = 1; return; }
    if (KIND == 2 || KIND == 3 || KIND == 4 || MT == 5) return;
    ranks_load();
    u16 fh = my_fopen(0x22C0, 0x22CB);                          /* "script.dat" "wb" */
    if (!fh) sd_fatal(FS_SCRIPT, 0x6C4);
    u16 mk = stk_mark();
    u16 tmp = stk_alloc(2), buf = stk_alloc(0x270);
    u16 fw[40];            /* PORT: 20 words in the original frame (more flights+convoy waypoints would
                              overwrite other locals); the shipped builders stay far below */
    u16 fa[16], fs[16];
    wr_u16w(fh, tmp, 4);
    wr_u16w(fh, tmp, 0);
    wr_u16w(fh, tmp, 1);
    if (KIND == 1) DSB(HDR + 6) |= DSB((u16)(0x2257 + (s16)(s8)module_active()));
    my_fwrite(HDR, 0x5A, 1, fh);

    /* ---- type 7 waypoints */
    s16 nwp = 0;
    for (s16 s = 0; s < 3; s++)
        for (s16 i = 0; i < (s16)NFL(s); i++) nwp = (s16)(nwp + (s16)DSW(FLT(s, i) + FL_NWP));
    for (s16 c = 0; c < (s16)DSW(PS_DS_convoy_count); c++) nwp = (s16)(nwp + (s16)DSW(CONV(c) + CV_NWP));
    u16 rec = xmalloc((u16)(nwp * 14));
    wr_u16w(fh, tmp, 7);
    wr_u16w(fh, tmp, (u16)nwp);
    fw[0] = 0;
    s16 di = 0, k = 1;
    for (s16 i = 0; i < (s16)DSW(FLT(0, 0) + FL_NWP); i++, di++) {
        u16 r = (u16)(rec + di * 14);
        DSB(r) = 0;
        DSB(r + 1) = (u8)i;
        rtl_memcpy((u16)(r + 2), (u16)(DSW(FLT(0, 0) + FL_WP) + i * 12), 12);
    }
    for (s16 i = 1; i < (s16)NFL(0); i++) {
        fw[k++] = (u16)di;
        for (s16 w = 0; w < (s16)DSW(FLT(0, i) + FL_NWP); w++, di++) {
            u16 r = (u16)(rec + di * 14);
            DSB(r) = 0;
            DSB(r + 1) = 0xFF;
            rtl_memcpy((u16)(r + 2), (u16)(DSW(FLT(0, i) + FL_WP) + w * 12), 12);
        }
    }
    for (s16 s = 1; s < 3; s++)
        for (s16 i = 0; i < (s16)NFL(s); i++) {
            fw[k++] = (u16)di;
            for (s16 w = 0; w < (s16)DSW(FLT(s, i) + FL_NWP); w++, di++) {
                u16 r = (u16)(rec + di * 14);
                DSB(r) = 0;
                DSB(r + 1) = 0xFF;
                rtl_memcpy((u16)(r + 2), (u16)(DSW(FLT(s, i) + FL_WP) + w * 12), 12);
            }
        }
    for (s16 c = 0; c < (s16)DSW(PS_DS_convoy_count); c++, di++) {
        u16 r = (u16)(rec + di * 14);         /* quirk: one record per convoy, whatever its waypoints */
        for (s16 w = 0; w < (s16)DSW(CONV(c) + CV_NWP); w++) {
            fw[k++] = (u16)di;
            DSB(r) = 0;
            DSB(r + 1) = 0xFF;
            rtl_memcpy((u16)(r + 2), (u16)(DSW(CONV(c) + CV_WP) + w * 12), 12);
        }
    }
    for (s16 i = 0; i < nwp; i++) {
        my_fwrite((u16)(rec + i * 14), 2, 1, fh);
        my_fwrite((u16)(rec + i * 14 + 2), 12, 1, fh);
    }
    xfree(rec);

    /* ---- type 1 aircraft */
    s16 nac = 0;
    for (s16 s = 0; s < 3; s++)
        for (s16 i = 0; i < (s16)NFL(s); i++) nac = (s16)(nac + (s16)DSW(FLT(s, i) + FL_PLANES));
    rec = xmalloc((u16)(nac * 0x16));
    di = 0;
    k = 0;
    for (s16 s = 0; s < 3; s++)
        for (s16 i = 0; i < (s16)NFL(s); i++) {
            fa[k++] = (u16)di;
            u16 f = FLT(s, i);
            for (s16 p = 0; p < (s16)DSW(f + FL_PLANES); p++, di++) {
                u16 r = (u16)(rec + di * 0x16);
                DSB(r) = (u8)DSW(f + FL_TYPE);
                DSB(r + 1) = DSB(f + FL_SVC);
                DSB(r + 2) = pilot_code(s, i, p);
                DSB(r + 3) = 0;
                DSB(r + 4) = 0x80;
                DSB(r + 5) = 0;
                DSB(r + 6) = 0x80;
                DSB(r + 7) = KIND == 0 ? (u8)rank_last(DSC(f + FL_SVC)) : DSB(0x4777);
                DSB(r + 8) = 0;
                DSB(r + 9) = 0;
                DSW(r + 0xA) = 0xFFFF;
                for (u16 b = 0xC; b < 0x16; b++) DSB(r + b) = 0xFF;
                u16 cm = stk_mark();
                u16 code = stk_alloc(10), cnt = stk_alloc(10);
                s16 nit = ord_n((s16)DSW(f + FL_TYPE), DSC(f + FL_LOAD));
                ord_items((s16)DSW(f + FL_TYPE), DSC(f + FL_LOAD), cnt, code);
                s16 slot = 0;
                for (s16 c = 0; c < nit; c++) {
                    u16 cd = DSW(code + c * 2);
                    u8 cv = DSB(cnt + c * 2);
                    if (cd > 0x13) continue;
                    if (cd == 0) DSB(r + 3) = 1;
                    else if (cd >= 10 && cd <= 16) {
                        if (slot == 0) { DSB(r + 0x10) = (u8)(cd - 10); DSB(r + 0x11) = cv; }
                        else if (slot == 1) { DSB(r + 0x12) = (u8)(cd - 10); DSB(r + 0x13) = cv; }
                        else if (slot == 2) { DSB(r + 0x14) = (u8)(cd - 10); DSB(r + 0x15) = cv; }
                        slot++;
                    } else if (cd == 17) { DSB(r + 0xC) = 0; DSB(r + 0xD) = cv; }
                    else if (cd == 18) { DSB(r + 0xE) = 1; DSB(r + 0xF) = cv; }
                    else if (cd == 19) { DSB(r + 0xE) = 0; DSB(r + 0xF) = cv; }
                }
                stk_release(cm);
            }
        }
    wr_u16w(fh, tmp, 1);
    wr_u16w(fh, tmp, (u16)nac);
    my_fwrite(rec, 0x16, nac, fh);
    xfree(rec);

    /* ---- type 2 ships */
    s16 nsh = 0;
    for (s16 c = 0; c < (s16)DSW(PS_DS_convoy_count); c++) nsh = (s16)(nsh + (s16)DSW(CONV(c) + CV_NSHIP));
    if (nsh != 0) {
        rec = xmalloc((u16)(nsh << 4));
        di = 0;
        k = 0;
        for (s16 c = 0; c < (s16)DSW(PS_DS_convoy_count); c++) {
            fs[k++] = (u16)di;
            u16 v = CONV(c);
            for (s16 j = 0; j < (s16)DSW(v + CV_NSHIP); j++, di++) {
                u16 r = (u16)(rec + (di << 4));
                DSB(r) = DSB(v + CV_TYPES + j);
                DSB(r + 1) = DSB(v + CV_SVC);
                DSW(r + 2) = 0;
                DSB(r + 6) = 0;
                DSB(r + 7) = 0;
                DSW(r + 4) = 0xFFFF;
                DSL(r + 8) = 0;
                DSL(r + 0xC) = 0;
            }
        }
        wr_u16w(fh, tmp, 2);
        wr_u16w(fh, tmp, (u16)nsh);
        my_fwrite(rec, 0x10, nsh, fh);
        xfree(rec);
    }

    /* ---- type 4 places */
    s16 npl = (s16)DSW(PS_DS_place_count);
    if (npl != 0) {
        rec = xmalloc((u16)(npl * 10));
        for (s16 i = 0; i < npl; i++) {
            u16 p = PLACE(i), r = (u16)(rec + i * 10);
            DSB(r) = DSB(p + PL_KIND);
            DSB(r + 1) = DSB(p + PL_SVC);
            DSL(r + 2) = DSL(p + PL_POS);
            DSL(r + 6) = DSL(p + PL_POS + 4);
        }
        wr_u16w(fh, tmp, 4);
        wr_u16w(fh, tmp, (u16)npl);
        my_fwrite(rec, 10, npl, fh);
        xfree(rec);
    }

    /* ---- type 5 groups */
    s16 nfl = (s16)(NFL(0) + NFL(1) + NFL(2));
    s16 ngr = (s16)(nfl + (s16)DSW(PS_DS_convoy_count));
    rec = xmalloc((u16)(ngr * 0x3A));
    rtl_memset(rec, 0, (u16)(ngr * 0x3A));   /* PORT: +14 (other +08) and +1F are malloc garbage in the original */
    di = 0;
    for (s16 s = 0; s < 3; s++)
        for (s16 i = 0; i < (s16)NFL(s); i++, di++) {
            u16 f = FLT(s, i), r = (u16)(rec + di * 0x3A);
            DSB(r) = 0;
            DSB(r + 1) = DSB(f + FL_FORM);
            DSB(r + 2) = DSB(f + FL_B07);
            DSB(r + 3) = DSB(f + FL_B04);
            DSB(r + 4) = DSB(f + FL_HDG);
            DSB(r + 5) = DSB(f + FL_CARR);
            DSB(r + 6) = DSB(f + FL_TK);
            DSB(r + 7) = 0xFF;
            if (DSB(r + 5)) DSW(r + 8) = DSW(f + FL_HOME) == 0 ? 0 : DSW(CONV(0) + CV_NSHIP);
            else DSW(r + 8) = DSW(f + FL_HOME);
            DSW(r + 0xA) = DSW(f + FL_PLANES);
            DSW(r + 0xC) = fa[di];
            DSW(r + 0xE) = fw[di];
            DSW(r + 0x10) = 0xFFFF;
            DSW(r + 0x12) = 1;
            u8 tk = DSB(f + FL_TK);
            if (tk == 1 || tk == 0) DSW(r + 0x14) = DSW(f + FL_TGT);
            else if (tk == 2) { DSB(r + 6) = 0; DSW(r + 0x14) = (u16)(nfl + (s16)DSW(f + FL_TGT)); }
            DSW(r + 0x16) = 0;
            DSW(r + 0x18) = 0;
            DSW(r + 0x1A) = (u16)(DSW(f + FL_NWP) - 1);
            DSW(r + 0x1C) = (u16)(fw[di] + 1);
            DSB(r + 0x1E) = DSB(f + FL_B09);
            DSB(r + 0x20) = 3;
            DSL(r + 0x24) = 50000;
            DSB(r + 0x21) = DSB(r + 0x22) = DSB(r + 0x23) = 0;
            DSW(r + 0x28) = DSW(r + 0x2A) = DSW(r + 0x2C) = 0;
            DSW(r + 0x2E) = 0xFFFF;
            for (int b = 0; b < 10; b += 2) { DSB(r + 0x30 + b) = 0xFF; DSB(r + 0x31 + b) = 0; }
        }
    k = 0;
    for (s16 c = 0; c < (s16)DSW(PS_DS_convoy_count); c++, di++, k++) {
        u16 v = CONV(c), r = (u16)(rec + di * 0x3A);
        DSB(r) = 1;
        DSB(r + 1) = DSB(v + CV_KIND);
        DSB(r + 2) = 0;
        DSB(r + 3) = 0;
        DSB(r + 4) = DSB(v + CV_CALL);
        DSB(r + 5) = 0;
        DSB(r + 6) = 0xFF;
        DSB(r + 7) = 0xFF;
        DSW(r + 8) = 0;
        DSW(r + 0xA) = DSW(v + CV_NSHIP);
        DSW(r + 0xC) = fs[k];
        DSW(r + 0xE) = fw[di];
        DSW(r + 0x10) = 0xFFFF;
        DSW(r + 0x12) = 0;
        DSW(r + 0x14) = 0;
        DSW(r + 0x16) = 0;
        DSW(r + 0x18) = 0;
        DSW(r + 0x1A) = (u16)(DSW(v + CV_NWP) - 1);
        DSW(r + 0x1C) = (u16)(fw[di] + 1);
        DSB(r + 0x1E) = 1;
        DSB(r + 0x1F) = 0;
        DSB(r + 0x20) = DSB(r + 0x21) = DSB(r + 0x22) = DSB(r + 0x23) = 0;
        DSL(r + 0x24) = 0;
        DSW(r + 0x28) = DSW(r + 0x2A) = DSW(r + 0x2C) = 0;
        DSW(r + 0x2E) = 0xFFFF;
        for (int b = 0; b < 10; b += 2) { DSB(r + 0x30 + b) = 0xFF; DSB(r + 0x31 + b) = 0; }
    }
    wr_u16w(fh, tmp, 5);
    wr_u16w(fh, tmp, (u16)ngr);
    my_fwrite(rec, 0x3A, ngr, fh);
    xfree(rec);

    /* ---- types 8/9 flight-plan messages */
    stattxt_open();
    fltplan_sim_on();
    fltplan_build();
    stattxt_close();
    u16 msgs = fltplan_messages();
    s16 nm = (s16)DSW(FLT(0, 0) + FL_NWP);
    if (nm == 0) sd_fatal(FS_SCRIPT, 0x87E);
    rec = xmalloc((u16)(nm * 2));
    DSW(rec) = 0;
    for (s16 i = 1; i < nm; i++)
        DSW(rec + i * 2) = (u16)(rtl__fstrlen(ds_far((u16)(msgs + (i - 1) * 4))) + DSW(rec + (i - 1) * 2) + 1);
    wr_u16w(fh, tmp, 8);
    wr_u16w(fh, tmp, (u16)nm);
    my_fwrite(rec, 2, nm, fh);
    xfree(rec);
    u16 nbytes = 0;
    for (s16 i = 0; i < nm; i++) nbytes = (u16)(rtl__fstrlen(ds_far((u16)(msgs + i * 4))) + nbytes + 1);
    wr_u16w(fh, tmp, 9);
    wr_u16w(fh, tmp, nbytes);
    for (s16 i = 0; i < nm; i++) {
        strcpy_far_src(buf, ds_far((u16)(msgs + i * 4)));
        my_fwrite(buf, (s16)(rtl_strlen(buf) + 1), 1, fh);
    }
    fltplan_free();
    my_fclose(fh);
    ranks_free();
    stk_release(mk);
}

/* PS 3164:349d wp_message(i) */
u16 wp_message(u16 i) { return DSW((u16)(PS_DS_wp_messages + i * 2)); }

/* ================================================================================================
 * mission file -> memory (shell_data.md §9)
 * ================================================================================================ */

static u16 alloc_or_null(u16 n, u16 size) { return n ? xmalloc((u16)(n * size)) : 0; }

/* PS 3164:34ad load_alloc */
void load_alloc(u16 nac, u16 nsh, u16 npl, u16 nwp, u16 ngr, u16 n3)
{
    mission_reset();
    DSW(LT_AC) = alloc_or_null(nac, 0x16);
    DSW(LT_SH) = alloc_or_null(nsh, 0x10);
    DSW(LT_PL) = alloc_or_null(npl, 0x0A);
    DSW(LT_WP) = alloc_or_null(nwp, 0x0E);
    DSW(LT_GRP) = alloc_or_null(ngr, 0x3A);
    DSW(LT_T3) = alloc_or_null(n3, 6);
    rtl_memset(DSW(LT_AC), 0, (u16)(nac * 0x16));
    rtl_memset(DSW(LT_SH), 0, (u16)(nsh << 4));
    rtl_memset(DSW(LT_PL), 0, (u16)(npl * 0x0A));
    rtl_memset(DSW(LT_WP), 0, (u16)(nwp * 0x0E));
    rtl_memset(DSW(LT_GRP), 0, (u16)(ngr * 0x3A));
    rtl_memset(DSW(LT_T3), 0, (u16)(n3 * 6));
    DSW(LN_T3) = DSW(LN_WP) = DSW(LN_GRP) = DSW(LN_PL) = DSW(LN_SH) = DSW(LN_AC) = 0;
}

/* PS 3164:3602 load_free */
void load_free(void)
{
    static const u16 t[6] = { LT_AC, LT_SH, LT_PL, LT_WP, LT_GRP, LT_T3 };
    for (int i = 0; i < 6; i++) if (DSW(t[i])) xfree(DSW(t[i]));
}

/* PS 3164:366d / 3694 / 36d0 / 36f6 / 3788 append a record */
void load_add3(u16 rec6) { rtl_memcpy((u16)(DSW(LT_T3) + DSW(LN_T3)++ * 6), rec6, 6); }
void load_add_aircraft(u16 a0)
{
    rtl_memcpy((u16)(DSW(LT_AC) + DSW(LN_AC) * 0x16), a0, 0x16);
    if (DSB(a0 + 2) == 0x2C) {
        DSW(0x61CE) = DSW(LN_AC);
        DSB(0x61D2) = DSB(a0 + 1);
    }
    DSW(LN_AC)++;
}
void load_add_ship(u16 a0) { rtl_memcpy((u16)(DSW(LT_SH) + (DSW(LN_SH)++ << 4)), a0, 0x10); }
void load_add_place(u16 a0) { rtl_memcpy((u16)(DSW(LT_PL) + DSW(LN_PL)++ * 10), a0, 10); }
void load_add_group(u16 a0) { rtl_memcpy((u16)(DSW(LT_GRP) + DSW(LN_GRP)++ * 0x3A), a0, 0x3A); }

/* PS 3164:371d load_add_waypoint(hdr[2], data) — 12 bytes of data for kind 0, else 4 */
void load_add_waypoint(u16 p0_ds, u16 data)
{
    u16 r = (u16)(DSW(LT_WP) + DSW(LN_WP) * 0xE);
    rtl_memcpy(r, p0_ds, 2);
    rtl_memcpy((u16)(r + 2), data, DSB(p0_ds) == 0 ? 12 : 4);
    DSW(LN_WP)++;
}

/* PS 3164:37af find_group(idx, kind) -> the group's start waypoint */
static s16 find_group(s16 idx, s8 kind)
{
    u16 g = DSW(LT_GRP);
    for (s16 i = 0; i < (s16)DSW(LN_GRP); i++, g = (u16)(g + 0x3A)) {
        if (DSC(g) != kind) continue;
        if ((s16)DSW(g + 0xC) <= idx && idx < (s16)(DSW(g + 0xC) + DSW(g + 0xA))) return (s16)DSW(g + 0xE);
    }
    sd_fatal(FS_SCRIPT, 0x96E);
    return -1;
}

/* PS 3164:3811 resolve_wp(far dst, i, first, start) */
static void resolve_wp(FarPtr dst, s16 i, s16 first, s16 start)
{
    s16 si = i ? (s16)(first + i - 1) : start;
    if (si <= -1) sd_fatal(FS_SCRIPT, 0x978);
    u16 w = (u16)(DSW(LT_WP) + si * 0xE);
    u16 m = stk_mark();
    u16 loc = stk_alloc(12);
    s16 kind = (s16)DSC(w);
    s16 ref = (s16)DSW(w + 2);
    switch ((u16)kind) {
    case 0: break;
    case 1:
        if (ref >= (s16)DSW(LN_GRP)) sd_fatal(FS_SCRIPT, 0x981);
        si = (s16)DSW((u16)(DSW(LT_GRP) + ref * 0x3A + 0xE));
        if (si <= -1) sd_fatal(FS_SCRIPT, 0x983);
        break;
    case 2: {
        if (ref >= (s16)DSW(LN_AC)) sd_fatal(FS_SCRIPT, 0x988);
        u16 a = (u16)(DSW(LT_AC) + ref * 0x16);
        si = (s16)DSW(a + 0xA) == -1 ? find_group(ref, 0) : (s16)DSW(a + 0xA);
        break;
    }
    case 3: {
        if (ref >= (s16)DSW(LN_SH)) sd_fatal(FS_SCRIPT, 0x991);
        u16 s = (u16)(DSW(LT_SH) + (ref << 4));
        si = (s16)DSW(s + 4) == -1 ? find_group(ref, 1) : (s16)DSW(s + 4);
        break;
    }
    case 4: {
        if (ref >= (s16)DSW(LN_T3)) sd_fatal(FS_SCRIPT, 0x99A);
        u16 t = (u16)(DSW(LT_T3) + ref * 6);
        si = (s16)DSW(t + 2) == -1 ? find_group(ref, 2) : (s16)DSW(t + 2);
        break;
    }
    case 5: {
        if (ref >= (s16)DSW(LN_PL)) sd_fatal(FS_SCRIPT, 0x9A3);
        u16 p = (u16)(DSW(LT_PL) + ref * 10);
        DSL(loc) = DSL(p + 2);
        DSL(loc + 4) = DSL(p + 6);
        DSL(loc + 8) = 0;
        si = -1;
        break;
    }
    default: sd_fatal(FS_SCRIPT, 0x9AB); break;
    }
    if (si >= (s16)DSW(LN_WP)) sd_fatal(FS_SCRIPT, 0x9AF);
    u16 src = si == -1 ? loc : (u16)(DSW(LT_WP) + si * 0xE + 2);
    for (u16 b = 0; b < 12; b++) fwb(dst, b, DSB(src + b));
    stk_release(m);
}

/* PS 3164:3ad3 code_skill / 3afa code_ace */
static s8 code_skill(s8 c) { return c < 0x2C ? (s8)((s16)c % 4) : c == 0x2C ? 4 : (s8)0xFF; }
static s16 code_ace(s8 c) { return (s16)((s16)c - 0x2D); }

/* PS 3164:3b08 load_places */
static void load_places(void)
{
    u16 r = DSW(LT_PL);
    for (s16 i = 0; i < (s16)DSW(LN_PL); i++, r = (u16)(r + 10)) {
        u16 p = PLACE(i);
        DSB(p + PL_KIND) = DSB(r);
        DSB(p + PL_SVC) = DSB(r + 1);
        DSB(p + PL_FLAG) = 1;
        DSW(p + PL_REC) = 0xFFFF;
        DSL(p + PL_POS) = DSL(r + 2);
        DSL(p + PL_POS + 4) = DSL(r + 6);
    }
    DSW(PS_DS_place_count) = DSW(LN_PL);
}

/* PS 3164:3b5f load_convoy(conv, g) */
static void load_convoy(u16 v, u16 g)
{
    DSB(v + CV_CALL) = (u8)(DSB(g + 4) + 0x40);
    DSW(v + CV_NSHIP) = DSW(g + 0xA);
    DSW(v + CV_NWP) = (u16)(DSW(g + 0x1A) + 1);
    DSB(v + CV_SVC) = DSB((u16)(DSW(LT_SH) + (DSW(g + 0xC) << 4) + 1));
    DSW(v + CV_WP) = xmalloc((u16)((DSW(g + 0x1A) + 1) * 12));
    for (s16 k = 0; k < (s16)DSW(g + 0xA); k++)
        DSB(v + CV_TYPES + k) = DSB((u16)(DSW(LT_SH) + ((DSW(g + 0xC) + k) << 4)));
    for (s16 k = 0; k < (s16)(DSW(g + 0x1A) + 1); k++)
        resolve_wp(ds_ptr((u16)(DSW(v + CV_WP) + k * 12)), k, (s16)DSW(g + 0x1C), (s16)DSW(g + 0xE));
    DSW(PS_DS_convoy_count)++;
}

/* PS 3164:3c1a load_flight(g) -> 1 if the player's flight */
static s8 load_flight(u16 g)
{
    s8 found = 0;
    u16 ac = (u16)(DSW(LT_AC) + DSW(g + 0xC) * 0x16);
    s8 side = DSB(ac + 1) == DSB(0x61D2) ? 0 : 1;
    s8 n = (s8)DSB(PS_DS_flight_count + side * 2);
    u16 f = FLT(side, n);
    DSB(f + FL_FORM) = DSB(g + 1);
    DSB(f + FL_HDG) = (u8)(DSB(g + 4) + 0x40);
    DSB(f + FL_CARR) = DSB(g + 5);
    DSW(f + FL_PLANES) = DSW(g + 0xA);
    DSW(f + FL_NWP) = (u16)(DSW(g + 0x1A) + 1);
    DSB(f + FL_LOAD) = (u8)(DSB(g + 7) + 1);
    DSB(f + FL_SVC) = DSB(ac + 1);
    DSW(f + FL_TYPE) = (u16)DSB(ac);
    DSW(f + FL_WP) = xmalloc((u16)((DSW(g + 0x1A) + 1) * 12));
    for (s16 k = 0; k < (s16)DSW(g + 0xA); k++) {
        s8 code = DSC((u16)(ac + k * 0x16 + 2));
        if (code == 0x2C) found = 1;
        s8 sk = code_skill(code);
        DSW(f + FL_ACE + k * 2) = (u8)sk == 0xFF ? (u16)code_ace(code) : 0xFFFF;
        DSB(f + FL_SKILL + k) = (u8)sk;
    }
    for (s16 k = 0; k < (s16)(DSW(g + 0x1A) + 1); k++)
        resolve_wp(ds_ptr((u16)(DSW(f + FL_WP) + k * 12)), k, (s16)DSW(g + 0x1C), (s16)DSW(g + 0xE));
    if (found) {
        if ((s16)DSW(g + 0x1A) > 10) sd_fatal(FS_SCRIPT, 0xA2B);
        DSW(PS_DS_wp_messages) = (u16)(s16)DSC((u16)(DSW(LT_WP) + DSW(g + 0xE) * 0xE + 1));
        for (s16 k = 1; k < (s16)(DSW(g + 0x1A) + 1); k++)
            DSW((u16)(PS_DS_wp_messages + k * 2)) =
                (u16)(s16)DSC((u16)(DSW(LT_WP) + (DSW(g + 0x1C) + k - 1) * 0xE + 1));
    }
    DSW(f + FL_HOME) = DSW(g + 8);
    if (DSB(g + 5) == 0) DSB(PLACE(DSW(f + FL_HOME)) + PL_FLAG) = (u8)found;
    DSW(PS_DS_flight_count + side * 2)++;
    return found;
}

/* PS 3164:3e0f load_groups -> player flight index (-1 none) */
static s16 load_groups(void)
{
    s8 k = (s8)0xFF;
    u16 v = PS_DS_convoys;
    u16 g = DSW(LT_GRP);
    for (s16 i = 0; i < (s16)DSW(LN_GRP); i++, g = (u16)(g + 0x3A)) {
        if (DSB(g) == 1) { load_convoy(v, g); v = (u16)(v + 0x14); }
        if (DSB(g) == 0 && load_flight(g)) k = (s8)(DSB(PS_DS_flight_count) - 1);
    }
    return (s16)k;
}

/* PS 3164:3e6d flight_to_front(k) */
static void flight_to_front(s8 k)
{
    if (k <= -1) sd_fatal(FS_SCRIPT, 0xA5F);
    if (k == 0) return;
    u8 tmp[0x24];
    for (int b = 0; b < 0x24; b++) tmp[b] = DSB(FLT(0, k) + b);
    for (s16 i = k; i != 0; i--)
        for (int b = 0; b < 0x24; b++) DSB(FLT(0, i) + b) = DSB(FLT(0, i - 1) + b);
    for (int b = 0; b < 0x24; b++) DSB(FLT(0, 0) + b) = tmp[b];
}

/* PS 3164:3efe load_finish(p) */
void load_finish(u16 p)
{
    DSW(0x61D0) = p;
    load_places();
    s16 k = load_groups();
    flight_to_front((s8)k);
}

/* ================================================================================================
 * 3555 tables.c — TABLES.DAT ("tables" DS:22CE "dat" DS:22D5 "rb" DS:22D9)
 * ================================================================================================ */

void tables_load(void)
{
    u16 f = my_fopen(make_name(0x22CE, -1, 0x22D5), 0x22D9);
    if (!f) sd_fatal(FS_TABLES, 0xAE);
    for (s16 i = 0; i < 10; i++) my_fread((u16)(PS_DS_tab_condition + i * 18), 2, 9, f);   /* 3555:000f */
    for (s16 i = 0; i < 12; i++) my_fread((u16)(PS_DS_tab_wind + i * 8), 2, 4, f);         /* 003c */
    for (s16 i = 0; i < 5; i++) my_fread((u16)(PS_DS_tab_skill + i * 8), 2, 4, f);         /* 0068 */
    for (s16 i = 0; i < 10; i++) my_fread((u16)(PS_DS_tab_pools + i * 12), 2, 6, f);       /* 0094 */
    for (s16 i = 0; i < 5; i++) my_fread((u16)(PS_DS_tab_ordsel + i * 24), 2, 12, f);      /* 00c1 */
    for (s16 i = 0; i < 5; i++) my_fread((u16)(PS_DS_tab_altband + i * 8), 4, 2, f);       /* 00ee */
    my_fread(PS_DS_tab_rolealt, 4, 4, f);                                                   /* 011a */
    my_fclose(f);
}
