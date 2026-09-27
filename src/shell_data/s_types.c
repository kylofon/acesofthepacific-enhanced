/* shell_data: mission-type modules miss\s_*.c (PS 3570..37fc), shell_data_missions.md — every
 * function re-read from the disassembly (work/sd/stypes.lst). Differences to the spec text, all
 * following the code: type 4 re-uses the heading of the previous flight for the next start point
 * (bp-4 is overwritten inside the loop); types 4, 8, 9 also call 2968:0446; in types 8/9 both
 * midpoints live in the one 3d50:16b0 buffer, so the enemy point is always mid(T, mid(P, T)). */
#include "shell_data/sd_priv.h"
#include "render3d/render3d.h"

#define KIND  DSC(MKIND)
#define DATE  DSW(PS_DS_mission_date)

/* flight field offsets (shell_data.md §6) */
enum { F_SVC = 0x00, F_ROLE = 0x02, F_LOAD = 0x03, F_04 = 0x04, F_HDG = 0x05, F_CARR = 0x06, F_07 = 0x07,
       F_TREF = 0x08, F_09 = 0x09, F_SKILL = 0x0A, F_ACE = 0x0E, F_SQ = 0x16, F_TYPE = 0x18, F_N = 0x1A,
       F_NWP = 0x1C, F_HOME = 0x1E, F_TGT = 0x20, F_WP = 0x22 };

void intercept_bombers(s8 service);
void mission_kamikaze(s8 service);
void mission_esct_stub(void);

/* SCOPY of one 12-byte position */
static void cp12(u16 d, u16 s)
{
    for (u16 k = 0; k < 12; k++) DSB(d + k) = DSB(s + k);
}
static void set_alt(u16 p, s32 a) { DSSL(p + 8) = a; }
static s32 ralt(s8 r) { return DSSL((u16)(0x651E + (s16)r * 4)); }                 /* role altitude */
static s32 band_min(s8 c) { return DSSL((u16)(0x652E + (s16)c * 8)); }
static s32 band_max(s8 c) { return DSSL((u16)(0x6532 + (s16)c * 8)); }
static u16 hdg(u16 a, u16 b) { return heading(ds_ptr(a), ds_ptr(b)); }
static u8 hb(u16 h) { return (u8)((s16)h >> 8); }
static void move(u16 a, u16 ang, s32 d, u16 out) { project(ds_ptr(a), ang, d, out); }
static s32 smul(s32 a, s32 b) { return (s32)((u32)a * (u32)b); }
static s32 labs32(s32 x) { return x < 0 ? (s32)(0u - (u32)x) : x; }     /* 0000:1c46 */
/* waypoint k of a waypoint block */
#define WP(p, k) ((u16)((p) + (k) * 12))

/* common epilogue pieces */
static void close_emd(s8 with_wf)
{
    flight_close();
    missinf_close();
    if (with_wf) wldfgen_close();
}

/* ---------------------------------------------------------------- type 0 (3570:0003) */
void mission_dace(s8 a0, s8 kind)
{
    s16 ace = (s16)a0;
    missinf_open();
    flight_open();
    mission_reset();
    u16 date = ace_duel_date(ace);
    DSB(MKIND) = (u8)kind;
    DSB(MTYPE) = 0;
    DATE = date;
    DSW(0x62DB) = 0xFFFF;
    DSW(0x6261) = 1;
    DSW(0x6263) = 1;
    DSW(0x636B) = 0xFFFF;
    DSW(0x6363) = (u16)ace;
    DSB(0x635F) = 0xFF;
    DSB(0x6355) = (u8)ace_service(ace);
    DSW(0x62DF) = 1;
    DSW(0x62DD) = (u16)ace_duel_player_type(ace);
    if (ace_status(ace, date) == 0) {
        s16 sq = ace_squadron(ace, date);
        s16 st = squad_station(sq, date);
        DSB(MAREA) = station_area(st);
        DSB(0x62C5) = (u8)enemy_service(DSC(0x6355));
        pick_base(1, st, -1, 0x67DE);
        DSW(0x636F) = 1;
        DSW(0x636D) = (u16)squad_aircraft(sq, date);
    } else if (ace_status(ace, date) == 1) {
        s16 st = ace_posting_station(ace, date);
        DSB(MAREA) = station_area(st);
        DSB(0x62C5) = (u8)enemy_service(DSC(0x6355));
        pick_base(1, st, -1, 0x67DE);
        DSW(0x636F) = 1;
        DSW(0x636D) = (u16)ace_posting_aircraft(ace, date);
    }
    DSB(0x6270) = (u8)rng_below(3);
    pick_base(0, -1, -1, 0x67D2);
    u16 m = lmid(0x67D2, 0x67DE);
    s8 c1 = ac_alt_band((s16)DSW(0x62DD));
    s8 c2 = ac_alt_band((s16)DSW(0x636D));
    if (c1 >= 4 && c2 >= 4) c1 = c2 = 4;
    else if (c1 > c2) c1 = c2;
    else c2 = c1;
    cp12(0x67EA, m);
    cp12(0x67F6, m);
    set_alt(0x67EA, band_min(c1));
    set_alt(0x67F6, band_min(c2));
    DSSL(0x680A) = DSSL(0x67F2);
    DSSL(0x6816) = DSSL(0x67FE);
    DSB(0x62CC) = (u8)c1;
    DSB(0x635C) = (u8)c2;
    u16 h;
    switch (DSC(0x6270)) {
    case 0:
        DSSL(0x67EA) += 25000;
        DSSL(0x67F6) -= 25000;
        h = hdg(0x67EA, 0x67F6);
        DSB(0x62CA) = hb(h);
        h = hdg(0x67F6, 0x67EA);
        DSB(0x635A) = hb(h);
        cp12(0x6802, 0x67F6);
        cp12(0x680E, 0x67EA);
        break;
    case 1:
        DSSL(0x67EA) += 5000;
        DSSL(0x67F6) -= 5000;
        h = hdg(0x67EA, 0x67F6);
        DSB(0x62CA) = DSB(0x635A) = hb(h);
        DSSL(0x67F2) += 3000;
        move(0x67EA, h, 4000000, 0x6802);
        move(0x67F6, h, 4000000, 0x680E);
        break;
    case 2:
        DSSL(0x67EA) += 5000;
        DSSL(0x67F6) -= 5000;
        h = hdg(0x67F6, 0x67EA);
        DSB(0x635A) = DSB(0x62CA) = hb(h);
        DSSL(0x67FE) += 3000;
        move(0x67EA, h, 4000000, 0x6802);
        move(0x67F6, h, 4000000, 0x680E);
        break;
    }
    DSW(0x62E1) = 2;
    DSW(0x62E7) = xmalloc(0x18);
    DSW(0x6371) = 2;
    DSW(0x6377) = xmalloc(0x18);
    cp12(DSW(0x62E7), 0x67EA);
    cp12(DSW(0x6377), 0x67F6);
    cp12(WP(DSW(0x62E7), 1), 0x6802);
    cp12(WP(DSW(0x6377), 1), 0x680E);
    DSB(0x62C9) = 4;
    DSB(0x6359) = 4;
    DSB(0x62CD) = 0;
    DSW(0x62E5) = 1;
    DSB(0x635D) = 0;
    DSW(0x6375) = 0;
    DSB(0x62CE) = 0;
    DSB(0x635E) = 0;
    if (KIND == 0) {
        acl_build(DSC(0x62C5), 6, 0, 0);
        acl_build(DSC(0x6355), 6, 1, 0);
    }
    close_emd(0);
}

/* ---------------------------------------------------------------- type 1 (35b3:000f) */
void mission_dsqd(s8 service, s8 kind)
{
    mission_reset();
    DSB(MKIND) = (u8)kind;
    DSB(MTYPE) = 1;
    mission_setup(service);
    missinf_open();
    flight_open();
    wldfgen_open();
    DSB(0x62C5) = (u8)service;
    DSB(0x6355) = (u8)enemy_service(service);
    add_flight(service, 0, 0);
    pick_base(0, -1, -1, 0x681A);
    DSB(0x6270) = (u8)rng_below(3);
    add_flight(DSC(0x6355), 1, 0);
    pick_base(1, -1, -1, 0x6826);
    u16 m = lmid(0x681A, 0x6826);
    s8 c1 = ac_alt_band((s16)DSW(0x62DD));
    s8 c2 = ac_alt_band((s16)DSW(0x636D));
    if (c1 >= 4 && c2 >= 4) c1 = c2 = 4;
    else if (c1 > c2) c1 = c2;
    else c2 = c1;
    cp12(0x6832, m);
    cp12(0x683E, m);
    set_alt(0x6832, band_min(c1));
    set_alt(0x683E, band_min(c2));
    DSSL(0x6852) = DSSL(0x683A);
    DSSL(0x685E) = DSSL(0x6846);
    DSB(0x62CC) = (u8)c1;
    DSB(0x635C) = (u8)c2;
    u16 h;
    switch (DSC(0x6270)) {
    case 0:
        DSSL(0x6832) += 19000;
        DSSL(0x683E) -= 19000;
        h = hdg(0x6832, 0x683E);
        DSB(0x62CA) = hb(h);
        h = hdg(0x683E, 0x6832);
        DSB(0x635A) = hb(h);
        cp12(0x684A, 0x6832);           /* own start, not the opponent's (quirk) */
        cp12(0x6856, 0x683E);
        break;
    case 1:
        DSSL(0x6832) += 5000;
        DSSL(0x683E) -= 5000;
        h = hdg(0x6832, 0x683E);
        DSB(0x62CA) = DSB(0x635A) = hb(h);
        DSSL(0x683A) += 3000;
        move(0x6832, h, 4000000, 0x684A);
        move(0x683E, h, 4000000, 0x6856);
        break;
    case 2:
        DSSL(0x6832) += 5000;
        DSSL(0x683E) -= 5000;
        h = hdg(0x683E, 0x6832);
        DSB(0x635A) = DSB(0x62CA) = hb(h);
        DSSL(0x6846) += 3000;
        move(0x6832, h, 4000000, 0x684A);
        move(0x683E, h, 4000000, 0x6856);
        break;
    }
    DSW(0x62E1) = 2;
    DSW(0x62E7) = xmalloc(0x18);
    DSW(0x6371) = 2;
    DSW(0x6377) = xmalloc(0x18);
    cp12(DSW(0x62E7), 0x6832);
    cp12(DSW(0x6377), 0x683E);
    cp12(WP(DSW(0x62E7), 1), 0x684A);
    cp12(WP(DSW(0x6377), 1), 0x6856);
    DSB(0x626E) = 0;
    DSB(0x62C9) = 4;
    DSB(0x6359) = 4;
    DSB(0x62CD) = 0;
    DSW(0x62E5) = 1;
    DSB(0x635D) = 0;
    DSW(0x6375) = 0;
    DSB(0x62CE) = 0;
    DSB(0x635E) = 0;
    if (KIND == 0) {
        acl_build(service, 5, 0, 0);
        acl_build(DSC(0x6355), 4, 1, 0);
    }
    wldfgen_close();
    flight_close();
    missinf_close();
}

/* common prologue of types 2..9: reset, header, setup, 2968/28e4/280a */
static void prologue(s8 service, s8 kind, u8 type)
{
    mission_reset();
    DSB(MKIND) = (u8)kind;
    DSB(MTYPE) = type;
    mission_setup(service);
    wldfgen_open();
    missinf_open();
    flight_open();
    DSB(0x62C5) = (u8)service;
}

/* ---------------------------------------------------------------- type 2 (35ed:0000) CAP */
void mission_cap(s8 service, s8 kind)
{
    u16 mk = stk_mark();
    u16 B = stk_alloc(12), EB = stk_alloc(12);
    prologue(service, kind, 2);
    DSB(0x6355) = (u8)enemy_service(service);
    add_flight(service, 0, 0);
    pick_base(0, -1, -1, B);
    DSB(0x6270) = (u8)rng_below(3);
    s32 R = smul(ac_range((s16)DSW(0x62DD)), 21128);
    s32 d = (R - smul(R, 25) / 100) / 7;
    d -= d >> 2;
    s32 bx = DSSL(B), bz = DSSL(B + 4);
    DSSL(0x6862) = bx - d;  DSSL(0x6866) = bz;
    DSSL(0x686E) = bx;      DSSL(0x6872) = bz + d;
    DSSL(0x687A) = bx + d;  DSSL(0x687E) = bz;
    DSSL(0x6886) = bx;      DSSL(0x688A) = bz - d;
    s16 n = (s16)(rng_below(4) + 1);
    for (s16 i = 0; i < n; i++) {
        DSW(FLT(1, i) + F_SQ) = 0xFFFF;
        add_flight(DSC(0x6355), 1, 0);
        u16 pi = (u16)(0x6862 + i * 12), pn = (u16)(0x686E + i * 12);   /* P[4] = E[0] for i = 3 */
        u16 m = lmid(pi, pn);
        s16 r = rng_below(3);
        if (r == 1) m = lmid(m, pn);
        else if (r != 0) m = lmid(pi, m);
        cp12((u16)(0x6892 + i * 12), m);
    }
    for (s16 i = 0; i < 4; i++) {
        u16 px = (u16)(0x6862 + i * 12), pz = (u16)(0x6866 + i * 12);
        while (labs32(DSSL(px)) >= 0xC15C00) DSL(px) = DSSL(px) < 0 ? 0xFF402AA0u : 0x00BFD560u;
        while (labs32(DSSL(pz)) >= 0xC15C00) DSL(pz) = DSSL(pz) < 0 ? 0xFF402AA0u : 0x00BFD560u;
    }
    pick_base(1, -1, -1, EB);
    s32 v = ac_cruise_alt((s16)DSW(0x62DD));
    DSW(0x62E1) = 7;
    u16 w = DSW(0x62E7) = xmalloc(0x54);
    cp12(WP(w, 0), B);
    cp12(WP(w, 1), 0x6862); set_alt(WP(w, 1), v);
    cp12(WP(w, 2), 0x686E); set_alt(WP(w, 2), v);
    cp12(WP(w, 3), 0x687A); set_alt(WP(w, 3), v);
    cp12(WP(w, 4), 0x6886); set_alt(WP(w, 4), v);
    cp12(WP(w, 5), 0x6862); set_alt(WP(w, 5), v);
    cp12(WP(w, 6), B);      set_alt(WP(w, 6), 10000);
    for (s16 i = 0; i < n; i++) {
        u16 f = FLT(1, i);
        DSW(f + F_NWP) = 2;
        u16 e = DSW(f + F_WP) = xmalloc(0x18);
        cp12(WP(e, 0), (u16)(0x6892 + i * 12)); set_alt(WP(e, 0), v);
        cp12(WP(e, 1), B);                      set_alt(WP(e, 1), v);
        DSB(f + F_HDG) = hb(hdg(0x6892, 0x689E));      /* E[0] -> E[1] for every flight */
        DSB(f + F_TREF) = 0;
        DSW(f + F_TGT) = 0;
        DSB(f + F_04) = 0;
        DSB(f + F_09) = 2;
    }
    DSB(0x62CD) = 0;
    DSW(0x62E5) = DSW(0x6261);
    DSB(0x62C9) = 1;
    DSB(0x62CE) = 0;
    if (KIND == 0) {
        acl_build(service, 5, 0, 0);
        acl_build(DSC(0x6355), 4, 1, 0);
    }
    close_emd(1);
    stk_release(mk);
}

/* ---------------------------------------------------------------- type 3 (3641:0005) fighter sweep */
void mission_fswp(s8 service, s8 kind)
{
    u16 mk = stk_mark();
    u16 wt = stk_alloc(6), B = stk_alloc(12), T = stk_alloc(12), route = stk_alloc(60), E = stk_alloc(36);
    for (u16 k = 0; k < 6; k++) DSB(wt + k) = DSB(0x22DC + k);
    prologue(service, kind, 3);
    DSB(0x6355) = (u8)enemy_service(service);
    add_flight(service, 0, 0);
    pick_base(0, -1, -1, B);
    s8 n = (s8)(rng_pick_weighted(wt, 3) + 1);
    for (s16 i = 0; i < n; i++) {
        DSW(FLT(1, i) + F_SQ) = 0xFFFF;
        add_flight(DSC(0x6355), 1, 0);
    }
    pick_base(1, -1, 0, T);
    if (DSW(T + 2) == 0xFFFF && DSW(T) == 0xFFFF) pick_ground_target(0, DSC(0x6355), B, T);
    s8 k = (s8)(rng_below(2) + 3);
    heading_deg(ds_ptr(T), ds_ptr(B));                       /* result discarded */
    s32 v = ac_cruise_alt((s16)DSW(0x62DD)), ve = v;
    if (band_max(ac_alt_band((s16)DSW(0x636D))) < ve) ve = ac_cruise_alt((s16)DSW(0x636D));
    s32 R = ac_range((s16)DSW(0x62DD));
    if (R > 800) R = 800;
    R = smul(R, 21128);
    s32 D = (R - smul(R, 35) / 100) / (s32)k;
    cp12(WP(route, 0), B);
    switch (k) {
    case 3:
        cp12(WP(route, 1), T); set_alt(WP(route, 1), v);
        cp12(WP(route, 2), B); set_alt(WP(route, 2), 10000);
        break;
    case 4:
        move(T, 0x11C6, D, WP(route, 1)); set_alt(WP(route, 1), v);
        cp12(WP(route, 2), T); set_alt(WP(route, 2), v);
        cp12(WP(route, 3), B); set_alt(WP(route, 3), 10000);
        break;
    case 5:
        move(T, 0x1FFE, D, WP(route, 1)); set_alt(WP(route, 1), v);
        cp12(WP(route, 2), T); set_alt(WP(route, 2), v);
        move(T, 0x038E, D, WP(route, 3)); set_alt(WP(route, 3), v);
        cp12(WP(route, 4), B); set_alt(WP(route, 4), 10000);
        break;
    }
    s32 r1 = ac_range((s16)DSW(0x62DD));
    s32 r2 = ac_range_ext((s16)DSW(0x62DD)) + r1;
    r1 = smul(r1, 21128);
    r2 = smul(r2, 21128);
    s32 L = dist(B, WP(route, 0)) + dist(WP(route, 0), WP(route, 1)) + dist(WP(route, 1), WP(route, 2));
    if (k > 3) L += dist(WP(route, 2), WP(route, 3));
    if (k > 4) L += dist(WP(route, 3), WP(route, 4));
    if (L > r2) {
        k = 3;
        cp12(WP(route, 1), T); set_alt(WP(route, 1), v);
        cp12(WP(route, 2), B); set_alt(WP(route, 2), 10000);
        L = dist(B, WP(route, 0)) + dist(WP(route, 0), WP(route, 1)) + dist(WP(route, 1), WP(route, 2));
    }
    if (L > r1) DSB(0x62C8) = (u8)ord_pick(3, (s8)DSW(0x62DD), 1, 0);
    cp12(WP(E, 0), T);
    for (s16 i = 1; i < n; i++) {
        s16 r = rng_below((s16)(k - 2));
        cp12(WP(E, i), lmid(T, WP(route, i + r)));
    }
    DSW(0x62E1) = (u16)(s16)k;
    u16 w = DSW(0x62E7) = xmalloc((u16)(k * 12));
    for (s16 i = 0; i < k; i++) cp12(WP(w, i), WP(route, i));
    for (s16 i = 0; i < n; i++) {
        u16 f = FLT(1, i);
        DSW(f + F_NWP) = 1;
        u16 e = DSW(f + F_WP) = xmalloc(0x0C);
        cp12(e, WP(E, i));
        set_alt(e, ve);
        DSB(f + F_HDG) = hb(hdg(WP(E, i), B));
        DSB(f + F_04) = 0;
        DSB(f + F_TREF) = 1;
        DSW(f + F_TGT) = (u16)(DSW(0x6267) - 1);
        DSB(f + F_09) = 2;
        DSB(f + F_LOAD) = 1;
    }
    DSB(0x62CD) = 0;
    DSW(0x62E5) = DSW(0x6261);
    DSB(0x62C9) = 4;
    DSB(0x62CE) = 0;
    if (KIND == 0) {
        acl_build(service, 5, 0, 0);
        acl_build(DSC(0x6355), 4, 1, 0);
    }
    close_emd(1);
    stk_release(mk);
}

/* ---------------------------------------------------------------- type 4 (36ab:0001) scramble */
void mission_scrm(s8 service, s8 kind)
{
    prologue(service, kind, 4);
    s8 e = enemy_service(service);
    add_flight(service, 0, 0);
    pick_base(0, -1, -1, 0x68B6);
    s8 n = rng_below(100) > 50 ? 2 : 1;
    for (s16 i = 0; i < n; i++) {
        DSW(FLT(1, i) + F_SQ) = 0xFFFF;
        add_flight(e, 1, 1);
    }
    DSB(0x6270) = (u8)rng_below(3);
    pick_base(1, -1, -1, 0x68C2);
    u16 h = hdg(0x68B6, 0x68C2);
    DSW(0x62E1) = 1;
    DSW(0x62E7) = xmalloc(0x0C);
    cp12(DSW(0x62E7), 0x68B6);
    DSB(0x62C9) = 5;
    DSB(0x62CE) = 0;
    DSB(0x62CD) = DSB(0x62CB) ? 2 : 1;
    DSW(0x62E5) = DSW(0x62E3);
    for (s16 i = 0; i < n; i++) {
        u16 f = FLT(1, i);
        s32 a = DSC(f + F_ROLE) == 3 ? 20000 : ralt(DSC(f + F_ROLE));
        DSW(f + F_NWP) = 3;
        u16 w = DSW(f + F_WP) = xmalloc(0x24);
        s32 d = DSC(0x6357) != 3 ? smul(i, 75000) + 75000 : smul(i, 75000) + 175000;
        move(0x68B6, h, d, 0x68CE);
        h = hdg(0x68CE, 0x68B6);             /* overwrites the start heading (quirk) */
        DSB(f + F_HDG) = hb(h);
        cp12(WP(w, 0), 0x68CE); set_alt(WP(w, 0), a);
        cp12(WP(w, 1), 0x68B6); set_alt(WP(w, 1), a);
        cp12(WP(w, 2), 0x68C2); set_alt(WP(w, 2), 10000);
        DSB(f + F_04) = 3;
        DSB(f + F_09) = 0;
        DSB(f + F_TREF) = DSB(0x62CB) ? 2 : 1;
        DSW(f + F_TGT) = DSW(0x62E3);
        DSB(f + F_LOAD) = (u8)ord_pick(DSC(MTYPE), (s8)DSW(f + F_TYPE), 0, 1);
    }
    if (KIND == 0) {
        acl_build(service, 5, 0, 0);
        acl_build(e, 0x3C, 1, 0);
    }
    close_emd(1);
}

/* ---------------------------------------------------------------- type 6 (36dd:000e) escort */
void mission_esct_stub(void) {}                   /* PS 36dd:0009: empty */

void mission_esct(s8 service, s8 kind)
{
    u16 mk = stk_mark();
    u16 wt = stk_alloc(4), A1 = stk_alloc(12), A2 = stk_alloc(12);
    DSW(wt) = DSW(0x22E2);
    DSW(wt + 2) = DSW(0x22E4);
    prologue(service, kind, 6);
    s8 e = enemy_service(service);
    add_flight(service, 0, 0);
    pick_base(0, -1, -1, 0x68DA);
    DSB(0x6270) = (u8)rng_below(3);
    s8 n = (s8)(rng_pick_weighted(wt, 2) + 1);
    for (s16 i = 0; i < n; i++) {
        DSW(FLT(1, i) + F_SQ) = 0xFFFF;
        add_flight(e, 1, 0);
    }
    pick_base(1, -1, -1, 0x68E6);
    DSW(0x63FB) = 0xFFFF;
    add_flight(service, 2, 1);
    pick_base(2, -1, -1, 0x68F2);
    if (DSB(0x63ED) == 1) {
        pick_ground_target(0, e, 0x68DA, 0x68FE);
        DSB(0x63ED) = 1;
        DSW(0x6405) = (u16)(DSW(0x6267) - 1);
    } else {
        pick_convoy(0, service, e, DSC(0x6357), 0x68DA, 0x68FE);
        DSB(0x63ED) = 0;
        DSW(0x6405) = (u16)(DSW(0x6261) + DSW(0x6263) + DSW(0x6265) + DSW(0x6269) - 1);
    }
    u16 m = lmid(0x68DA, 0x68FE);
    cp12(A2, m);
    cp12(A1, A2);
    s32 R = smul(ac_range((s16)DSW(0x62DD)), 21128);
    if (DSC(0x62C8) > 7) R += smul(ac_range_ext((s16)DSW(0x62DD)), 21128);
    s32 d0 = dist(0x68DA, m);
    R /= 4;
    s32 off = d0 < R ? (R - d0) >> 1 : 0;
    DSSL(A1) += off;
    DSSL(A2) -= off;
    s32 ab = ralt(DSC(0x63E7)), ae = ab;
    if (band_max(ac_alt_band((s16)DSW(0x636D))) < ae) ae = ac_cruise_alt((s16)DSW(0x636D));
    DSW(0x62E1) = 5;
    u16 w = DSW(0x62E7) = xmalloc(0x3C);
    DSW(0x6401) = 4;
    u16 bw = DSW(0x6407) = xmalloc(0x30);
    DSW(0x6371) = 1;
    u16 ew = DSW(0x6377) = xmalloc(0x0C);
    cp12(WP(w, 0), 0x68DA);
    cp12(WP(w, 1), A1);     set_alt(WP(w, 1), ab);
    cp12(WP(w, 2), 0x68FE); set_alt(WP(w, 2), ab);
    cp12(WP(w, 3), A2);     set_alt(WP(w, 3), ab);
    cp12(WP(w, 4), 0x68DA); set_alt(WP(w, 4), 10000);
    cp12(WP(bw, 0), A1);     set_alt(WP(bw, 0), ab);
    cp12(WP(bw, 1), 0x68FE); set_alt(WP(bw, 1), ab);
    cp12(WP(bw, 2), A2);     set_alt(WP(bw, 2), ab);
    cp12(WP(bw, 3), 0x68F2); set_alt(WP(bw, 3), 10000);
    cp12(ew, lmid(A1, 0x68FE));
    set_alt(ew, ae);
    if (n > 1) {
        DSW(0x6395) = 1;
        u16 e1 = DSW(0x639B) = xmalloc(0x0C);
        cp12(e1, 0x68FE);
        set_alt(e1, ae);
        DSB(0x6381) = DSB(0x63ED);
        DSW(0x6399) = DSW(0x6405);
    }
    DSB(0x62C9) = 6;
    DSB(0x6359) = 5;
    DSB(0x63E9) = 3;
    DSB(0x62CD) = 0;
    DSW(0x62E5) = (u16)(DSW(0x6261) + DSW(0x6263));
    DSB(0x635D) = DSB(0x63ED);
    DSW(0x6375) = DSW(0x6405);
    DSB(0x62CE) = 0;
    DSB(0x635E) = 2;
    DSB(0x63EE) = 1;
    if (KIND == 0) {
        acl_build(service, 5, 0, 0);
        acl_build(DSC(0x6355), 4, 1, 0);
        acl_build(service, 0x38, 2, 0);
    }
    close_emd(1);
    stk_release(mk);
}

/* ---------------------------------------------------------------- type 8 (372e:000c) anti-shipping */
void mission_anti(s8 service, s8 kind)
{
    u16 mk = stk_mark();
    u16 P = stk_alloc(12), T = stk_alloc(12), EB = stk_alloc(12);
    s16 w0 = (s16)DSW(0x22E6), w1 = (s16)DSW(0x22E8);
    memzero(EB, 12);                  /* PORT: EB is an uninitialised stack local when there is no
                                         enemy flight (62CA/63EA then head at garbage); zeroed */
    s8 A = 0, Bf = 0;
    prologue(service, kind, 8);
    s8 e = enemy_service(service);
    DSB(0x6355) = (u8)e;
    add_flight(service, 0, 1);
    pick_base(0, -1, -1, P);
    if (rng_below(100) > 50) {
        add_flight(DSC(0x6355), 1, 0);
        pick_base(1, -1, -1, EB);
        A = 1;
    }
    if ((DSC(0x62C7) == 0 && rng_below(100) <= w0) || (DSC(0x62C7) > 0 && rng_below(100) <= w1)) Bf = 1;
    DSB(0x626F) = (u8)rng_below(4);
    pick_convoy(0, service, e, DSC(0x62C7), P, T);
    s32 a = ralt(DSC(0x62C7)), ae = a;
    if (A && band_max(ac_alt_band((s16)DSW(0x636D))) < ae) ae = ac_cruise_alt((s16)DSW(0x636D));
    DSW(0x62E1) = 3;
    u16 w = DSW(0x62E7) = xmalloc(0x24);
    cp12(WP(w, 0), P);
    cp12(WP(w, 1), T); set_alt(WP(w, 1), a);
    cp12(WP(w, 2), P); set_alt(WP(w, 2), 10000);
    DSB(0x62CA) = hb(hdg(P, EB));
    DSB(0x62C9) = 3;
    DSB(0x62CD) = 2;
    DSW(0x62E5) = (u16)(DSW(0x6269) - 1);
    DSB(0x62CE) = 0;
    if (A) {
        DSW(0x6371) = 1;
        u16 ew = DSW(0x6377) = xmalloc(0x0C);
        u16 m1 = lmid(P, T);
        u16 m2 = lmid(T, m1);              /* same static buffer: m1 == m2 */
        u16 X = dist(T, m2) < 25000 ? m1 : m2;
        cp12(ew, X);
        set_alt(ew, ae);
        DSB(0x635A) = hb(hdg(T, P));
        DSB(0x6359) = 5;
        DSB(0x635D) = 2;
        DSW(0x6375) = (u16)(DSW(0x6269) - 1);
        DSB(0x635E) = 2;
        DSB(0x6358) = 1;
    }
    if (Bf) {
        add_flight(service, 2, 0);
        DSW(0x6401) = 3;
        u16 bw = DSW(0x6407) = xmalloc(0x24);
        cp12(WP(bw, 0), P); set_alt(WP(bw, 0), 10000);
        cp12(WP(bw, 1), T); set_alt(WP(bw, 1), a);
        cp12(WP(bw, 2), P); set_alt(WP(bw, 2), 10000);
        DSB(0x63EA) = hb(hdg(P, EB));
        DSB(0x63E9) = 6;
        DSB(0x63ED) = 0;
        DSW(0x6405) = 0;
        DSB(0x63EE) = 0;
    }
    if (KIND == 0) acl_build(service, 0x1D, 0, 0);
    close_emd(1);
    stk_release(mk);
}

/* ---------------------------------------------------------------- type 7 (3769) intercept */
#define P7  0x6922
#define T7  0x690A
#define EB7 0x6916

static bool pos_invalid(u16 p)
{
    return DSW(p + 2) == 0xFFFF && DSW(p) == 0xFFFF && DSW(p + 6) == 0xFFFF && DSW(p + 4) == 0xFFFF;
}

/* PS 3769:0009 intercept_bombers(service) */
void intercept_bombers(s8 service)
{
    add_flight(DSC(0x63E5), 2, 1);
    if (DSB(0x63ED) == 1 || DSC(0x63E7) == 3) {
        pick_ground_target(0, service, P7, T7);
        if (pos_invalid(T7)) {
            cp12(T7, P7);
            DSB(0x63ED) = DSB(0x62CB) ? 2 : 1;
            DSW(0x6405) = DSW(0x62E3);
        } else {
            DSB(0x63ED) = 1;
            DSW(0x6405) = (u16)(DSW(0x6267) - 1);
        }
    } else {
        pick_convoy(0, service, service, DSC(0x63E7), P7, T7);
        if (pos_invalid(T7)) {
            cp12(T7, P7);
            DSB(0x63ED) = DSB(0x62CB) ? 2 : 1;
            DSW(0x6405) = DSW(0x62E3);
        } else {
            DSB(0x63ED) = 2;
            DSW(0x6405) = (u16)(DSW(0x6269) - 1);
        }
    }
    add_escort_flights(DSC(0x692E), 1, 0);
    pick_base(2, -1, -1, EB7);
    u16 X = lmid(EB7, T7);
    s32 r1 = ac_range((s16)DSW(0x62DD));
    s32 r2 = ac_range_ext((s16)DSW(0x62DD)) + r1;
    r1 = smul(r1, 21128);
    r2 = smul(r2, 21128);
    s32 s;
    for (;;) {
        s = dist(P7, X) + dist(X, P7);
        if (s > r1 && s > r2) X = lmid(X, T7);
        if (!(s > r1 && s > r2)) break;
    }
    if (dist(X, T7) < 800000) move(T7, hdg(P7, T7), 800000, X);
    if (s > r1 && DSC(0x62C8) < 7) DSB(0x62C8) = (u8)ord_pick(7, (s8)DSW(0x62DD), 1, 0);
    DSW(0x62E1) = 3;
    u16 w = DSW(0x62E7) = xmalloc(0x24);
    cp12(WP(w, 0), P7);
    cp12(WP(w, 1), X);  set_alt(WP(w, 1), ralt(DSC(0x63E7)) + 10000);
    cp12(WP(w, 2), P7); set_alt(WP(w, 2), 10000);
    for (s8 i = (s8)DSB(0x6263); i > 0; i--) {
        u16 f = FLT(1, i - 1);
        DSW(f + F_NWP) = 3;
        u16 ew = DSW(f + F_WP) = xmalloc(0x24);
        cp12(WP(ew, 0), X);   set_alt(WP(ew, 0), ralt(DSC(0x63E7)));
        cp12(WP(ew, 1), T7);  set_alt(WP(ew, 1), ralt(DSC(0x63E7)));
        cp12(WP(ew, 2), EB7); set_alt(WP(ew, 2), 10000);
        DSB(f + F_04) = 6;
        DSB(f + F_TREF) = 0;
        DSW(f + F_TGT) = (u16)(DSW(0x6261) + DSW(0x6263));
        DSB(f + F_09) = 1;
        DSB(f + F_LOAD) = 1;
    }
    DSW(0x6401) = 3;
    u16 bw = DSW(0x6407) = xmalloc(0x24);
    cp12(WP(bw, 0), X);   set_alt(WP(bw, 0), ralt(DSC(0x63E7)));
    cp12(WP(bw, 1), T7);  set_alt(WP(bw, 1), ralt(DSC(0x63E7)));
    cp12(WP(bw, 2), EB7); set_alt(WP(bw, 2), 10000);
    DSB(0x63E9) = 3;
    if (KIND == 0) {
        acl_build(service, 5, 0, 0);
        acl_build(DSC(0x6355), 4, 1, 0);
        acl_build(DSC(0x63E5), 0x38, 2, 0);
    }
}

/* PS 3769:04de mission_kamikaze(service) — "kamikaze" DS:22EA "dat" DS:22F3 "rb" DS:22F7 */
void mission_kamikaze(s8 service)
{
    u16 mk = stk_mark();
    u16 k = stk_alloc(20), S = stk_alloc(12);
    add_flight(DSC(0x63E5), 2, 1);
    u16 f = my_fopen(make_name(0x22EA, -1, 0x22F3), 0x22F7);
    if (f) {
        my_fread(k, 2, 10, f);
        s16 r = rng_below(5);
        DSW(0x63FD) = DSC(0x692E) == 3 ? DSW((u16)(k + r * 2)) : DSW((u16)(k + r * 4));
        DSB(0x63E7) = (u8)ac_class((s16)DSW(0x63FD));
        DSB(0x63E8) = (u8)ord_pick(7, (s8)DSW(0x63FD), 0, 2);
        my_fclose(f);
    }
    if (DSB(0x62CB)) {
        cp12(T7, P7);
        DSB(0x63ED) = 2;
        DSW(0x6405) = DSW(0x62E3);
    } else intercept_bombers(service);
    add_escort_flights(DSC(0x692E), 1, 0);
    pick_base(2, -1, -1, EB7);
    move(T7, hdg(T7, EB7), 75000, S);
    DSW(0x62E1) = 3;
    u16 w = DSW(0x62E7) = xmalloc(0x24);
    cp12(WP(w, 0), P7);
    cp12(WP(w, 1), S);  set_alt(WP(w, 1), ralt(DSC(0x63E7)) + 10000);
    cp12(WP(w, 2), P7); set_alt(WP(w, 2), 10000);
    for (s8 i = (s8)DSB(0x6263); i != 0; i--) {
        u16 fl = FLT(1, i - 1);
        DSW(fl + F_NWP) = 3;
        u16 ew = DSW(fl + F_WP) = xmalloc(0x24);
        cp12(WP(ew, 0), S);   set_alt(WP(ew, 0), 60000);
        cp12(WP(ew, 1), T7);  set_alt(WP(ew, 1), 60000);
        cp12(WP(ew, 2), EB7); set_alt(WP(ew, 2), 10000);
        DSB(fl + F_04) = 6;
        DSB(fl + F_TREF) = 0;
        DSW(fl + F_TGT) = (u16)(DSW(0x6261) + DSW(0x6263));
        DSB(fl + F_09) = 0;
        DSB(fl + F_LOAD) = 1;
    }
    DSW(0x6401) = 3;
    u16 bw = DSW(0x6407) = xmalloc(0x24);
    cp12(WP(bw, 0), S);   set_alt(WP(bw, 0), ralt(DSC(0x63E7)));
    cp12(WP(bw, 1), T7);  set_alt(WP(bw, 1), ralt(DSC(0x63E7)));
    cp12(WP(bw, 2), EB7); set_alt(WP(bw, 2), 10000);
    DSB(0x63E9) = 2;
    DSB(MTYPE) = 10;
    if (KIND == 0) {
        acl_build(service, 5, 0, 0);
        acl_build(DSC(0x6355), 4, 1, 0);
        acl_build(DSC(0x63E5), 0x3C, 2, 0);
    }
    stk_release(mk);
}

/* PS 3769:0866 mission_intr(service, kind) */
void mission_intr(s8 service, s8 kind)
{
    prologue(service, kind, 7);
    s8 e = enemy_service(service);
    DSB(0x692E) = DSB(0x6355) = DSB(0x63E5) = (u8)e;
    add_flight(service, 0, 0);
    pick_base(0, -1, -1, P7);
    if (service == 0 && DSC(0x63E7) != 3 && DATE > 0x3FEC && rng_below(100) <= 0x41) mission_kamikaze(service);
    else intercept_bombers(service);
    DSB(0x62C9) = 5;
    DSB(0x62CE) = 0;
    DSB(0x63EE) = 1;
    DSB(0x62CD) = DSB(0x63ED);
    DSW(0x62E5) = DSW(0x6405);
    close_emd(1);
}

/* ---------------------------------------------------------------- type 9 (37fc:0005) ground attack */
void mission_grnd(s8 service, s8 kind)
{
    u16 mk = stk_mark();
    u16 P = stk_alloc(12), EB = stk_alloc(12), T = stk_alloc(12);
    s16 w0 = (s16)DSW(0x22FA), w1 = (s16)DSW(0x22FC);
    memzero(EB, 12);                  /* PORT: as type 8 (only read when an enemy flight exists) */
    s8 A = 0, Bf = 0;
    prologue(service, kind, 9);
    DSB(0x6355) = (u8)enemy_service(service);
    add_flight(service, 0, 1);
    pick_base(0, -1, -1, P);
    DSB(0x626F) = (u8)rng_below(4);
    if (rng_below(100) > 50) {
        add_flight(DSC(0x6355), 1, 0);
        pick_base(1, -1, -1, EB);
        A = 1;
    }
    if ((DSC(0x62C7) == 0 && rng_below(100) <= w0) || (DSC(0x62C7) > 0 && rng_below(100) <= w1)) Bf = 1;
    pick_ground_target(0, DSC(0x6355), P, T);
    s32 a = ralt(DSC(0x62C7)), ae = a;
    s16 j = rng_below(20);
    j = j > 10 ? (s16)((j - 20) * 1000) : (s16)(j * 1000);
    ae += j;
    if (A && band_max(ac_alt_band((s16)DSW(0x636D))) < ae) ae = ac_cruise_alt((s16)DSW(0x636D));
    DSW(0x62E1) = 3;
    u16 w = DSW(0x62E7) = xmalloc(0x24);
    cp12(WP(w, 0), P);
    cp12(WP(w, 1), T); set_alt(WP(w, 1), a);
    cp12(WP(w, 2), P); set_alt(WP(w, 2), 10000);
    DSB(0x62CA) = hb(hdg(P, T));
    DSB(0x62C9) = 3;
    DSB(0x62CD) = 1;
    DSW(0x62E5) = (u16)(DSW(0x6267) - 1);
    DSB(0x62CE) = 0;
    if (A) {
        DSW(0x6371) = 1;
        u16 ew = DSW(0x6377) = xmalloc(0x0C);
        u16 m1 = lmid(P, T);
        u16 m2 = lmid(T, m1);              /* same static buffer: m1 == m2 */
        u16 X = dist(EB, m2) < 25000 ? m1 : m2;
        cp12(ew, X);
        set_alt(ew, ae);
        DSB(0x635A) = hb(hdg(T, P));
        DSB(0x6359) = 5;
        DSB(0x635D) = 1;
        DSW(0x6375) = DSW(0x62E5);
        DSB(0x635E) = 2;
        DSB(0x6358) = 1;
    }
    if (Bf) {
        add_flight(service, 2, 0);
        DSW(0x6401) = 3;
        u16 bw = DSW(0x6407) = xmalloc(0x24);
        cp12(WP(bw, 0), P); set_alt(WP(bw, 0), 10000);
        cp12(WP(bw, 1), T); set_alt(WP(bw, 1), a);
        cp12(WP(bw, 2), P); set_alt(WP(bw, 2), 10000);
        DSB(0x63EA) = hb(hdg(P, T));
        DSB(0x63E9) = 6;
        DSB(0x63ED) = 0;
        DSW(0x6405) = 0;
        DSB(0x63EE) = 0;
    }
    if (KIND == 0) {
        acl_build(service, 0x15, 0, 0);
        acl_build(DSC(0x6355), 4, 1, 0);
    }
    close_emd(1);
    stk_release(mk);
}
