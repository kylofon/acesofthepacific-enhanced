/* Borland C++ 3.0 RTL: qsort, rand, time and date, getenv/tzset, _lrotl, labs, and the
 * #pragma startup table that c0 runs before main. Addresses INTRO / PS / AP. */
#include "platform/rtl_priv.h"

#include <time.h>

#include "codeptr.h"
#include "host.h"
#include "prog.h"

/* ------------------------------------------------------------------ qsort (bit-exact) */

/* PS 0000:3a98 / AP 0000:433e: exchange qWidth bytes (odd byte first, then words) */
static void qs_swap(u16 a, u16 b)
{
    u16 w = DSW(RL(qs_width));
    u16 di = a, si = b;
    if (w & 1) {
        u8 t = DSB(di);
        DSB(di) = DSB(si);
        DSB(si) = t;
        di++;
        si++;
    }
    for (u16 i = w >> 1; i; i--) {
        u16 t = DSW(di);
        DSW(di) = DSW(si);
        DSW(si) = t;
        di = (u16)(di + 2);
        si = (u16)(si + 2);
    }
}

static s16 qs_cmp(u16 a, u16 b)
{
    RtlCmpFn f = (RtlCmpFn)codeptr_lookup(ds_far(RL(qs_cmp)));
    return f(a, b);
}

/* PS 0000:3ac4 / AP 0000:436a qSortHelp(n, base) */
static void qs_help(u16 n, u16 base)
{
    const u16 W = DSW(RL(qs_width));
    u16 di = base;
    for (;;) {
        if (n <= 2) {
            if (n == 2) {
                u16 hi = (u16)(di + W);
                if (qs_cmp(di, hi) > 0) qs_swap(hi, di);
            }
            return;
        }
        u16 hi = (u16)(di + (u16)(n - 1) * W);       /* [bp-2] */
        u16 si = (u16)(di + (u16)(n >> 1) * W);      /* the middle */
        if (qs_cmp(si, hi) > 0) qs_swap(hi, si);
        if (qs_cmp(si, di) > 0) qs_swap(di, si);
        else if (qs_cmp(di, hi) > 0) qs_swap(hi, di);
        if (n == 3) { qs_swap(si, di); return; }
        u16 eq = (u16)(di + W);                      /* [bp-4] */
        si = eq;
        s16 c;
        for (;;) {                                   /* 442f */
            c = qs_cmp(si, di);
            if (c <= 0) {
                if (c == 0) { qs_swap(eq, si); eq = (u16)(eq + W); }
                if (si >= hi) goto partitioned;
                si = (u16)(si + W);
                continue;
            }
            if (si < hi) {
                for (;;) {                           /* 4443 */
                    c = qs_cmp(di, hi);
                    if (c >= 0) {
                        qs_swap(hi, si);
                        if (c != 0) { si = (u16)(si + W); hi = (u16)(hi - W); }
                        break;
                    }
                    hi = (u16)(hi - W);
                    if (!(si < hi)) break;
                }
            }
            if (!(si < hi)) break;
        }
partitioned:
        if (qs_cmp(si, di) <= 0) si = (u16)(si + W);
        u16 r = (u16)(si - W), l = di;               /* [bp-8], [bp-6] */
        while (l < eq && r >= eq) {
            qs_swap(r, l);
            l = (u16)(l + W);
            r = (u16)(r - W);
        }
        u16 nl = (u16)((u16)(si - eq) / W);
        u16 nr = (u16)((u16)(di + (u16)(n * W) - si) / W);
        if (nr < nl) {
            qs_help(nr, si);
            n = nl;
        } else {
            qs_help(nl, di);
            n = nr;
            di = si;
        }
    }
}

/* PS 0000:3c66 / AP 0000:450c qsort(base, n, width, cmp) */
void rtl_qsort(u16 base_ds, u16 n, u16 width, FarPtr cmp)
{
    DSW(RL(qs_width)) = width;
    if (width == 0) return;
    ds_far_wr(RL(qs_cmp), cmp);
    qs_help(n, base_ds);
}

/* ------------------------------------------------------------------ rand */

/* INTRO 0000:0aa6 / AP 0000:21dc srand */
void rtl_srand(u16 seed)
{
    DSL(RL(rand_seed)) = seed;
}

/* INTRO 0000:0ab7 / AP 0000:21ed rand */
s16 rtl_rand(void)
{
    u32 s = DSL(RL(rand_seed)) * 0x015A4E35u + 1;
    DSL(RL(rand_seed)) = s;
    return (s16)((s >> 16) & 0x7FFF);
}

/* ------------------------------------------------------------------ small ones */

/* INTRO 0000:0996 / PS 0000:1d08 / AP 0000:20c1 _lrotl */
u32 rtl__lrotl(u32 value, s16 count)
{
    int c = count & 0x1F;
    return c ? (value << c) | (value >> (32 - c)) : value;
}

/* PS 0000:1c46 / AP 0000:1fff labs */
s32 rtl_labs(s32 x) { return x < 0 ? (s32)(0u - (u32)x) : x; }

/* ------------------------------------------------------------------ date and time (host clock) */

/* INTRO 0000:05fc / PS 0000:1946 / AP 0000:1c96 getdate: INT 21h/2Ah -> {int year; char day, mon} */
void rtl_getdate(u16 date_ds)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    DSW(date_ds) = (u16)(tm->tm_year + 1900);
    DSB(date_ds + 2) = (u8)tm->tm_mday;
    DSB(date_ds + 3) = (u8)(tm->tm_mon + 1);
}

/* INTRO 0000:060f / PS 0000:1959 / AP 0000:1ca9 gettime: INT 21h/2Ch -> {min, hour, hund, sec} */
void rtl_gettime(u16 time_ds)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    DSB(time_ds + 0) = (u8)tm->tm_min;
    DSB(time_ds + 1) = (u8)tm->tm_hour;
    DSB(time_ds + 2) = 0;
    DSB(time_ds + 3) = (u8)tm->tm_sec;
}

/* INTRO 0000:275c / PS 0000:33de getenv */
u16 rtl_getenv(u16 name_ds)
{
    if (!name_ds) return 0;
    u8 ah = DSB(name_ds);
    u16 cx = ds_strlen(name_ds);
    if (cx == 0) return 0;
    u16 pp = DSW(RL(environ));
    if (!pp) return 0;
    for (;; pp = (u16)(pp + 2)) {
        u16 e = DSW(pp);
        if (!e) return 0;
        u8 c = DSB(e);
        if (!c) return 0;
        if (c != ah) continue;
        if (DSB((u16)(e + cx)) != '=') continue;
        bool eq = true;
        for (u16 i = 0; i < cx; i++) if (DSB((u16)(name_ds + i)) != DSB((u16)(e + i))) { eq = false; break; }
        if (eq) return (u16)(e + cx + 1);
    }
}

static bool ctype_is(u8 c, u8 mask) { return (DSB((u16)(RL(ctype1) + c)) & mask) != 0; }

/* INTRO 0000:3481 / PS 0000:4343 tzset (#pragma startup, priority 30): TZ=AAA[+-]h[BBB] or EST5EDT */
void rtl_tzset(void)
{
    u16 di = rtl_getenv(RL(tz_str));
    bool ok = di && ds_strlen(di) >= 4 && ctype_is(DSB(di), 0xC) && ctype_is(DSB(di + 1), 0xC) &&
              ctype_is(DSB(di + 2), 0xC) &&
              (DSB(di + 3) == '-' || DSB(di + 3) == '+' || ctype_is(DSB(di + 3), 2)) &&
              (ctype_is(DSB(di + 3), 2) || ctype_is(DSB(di + 4), 2));
    u16 tzn = RL(tzname);
    if (!ok) {
        DSW(RL(daylight)) = 1;
        DSL(RL(timezone)) = 18000;                   /* EST: 5 h */
        rtl_strcpy(DSW(tzn), RL(est_str));
        rtl_strcpy(DSW((u16)(tzn + 2)), RL(edt_str));
        return;
    }
    rtl_memset(DSW((u16)(tzn + 2)), 0, 4);
    rtl_strncpy(DSW(tzn), di, 3);
    DSB((u16)(DSW(tzn) + 3)) = 0;
    DSL(RL(timezone)) = (u32)(rtl_atol((u16)(di + 3)) * 3600);
    DSW(RL(daylight)) = 0;
    for (u16 si = 3; DSB((u16)(di + si)); si++) {
        if (!ctype_is(DSB((u16)(di + si)), 0xC)) continue;
        if (ds_strlen((u16)(di + si)) < 3) return;
        if (!ctype_is(DSB((u16)(di + si + 1)), 0xC) || !ctype_is(DSB((u16)(di + si + 2)), 0xC)) return;
        rtl_strncpy(DSW((u16)(tzn + 2)), (u16)(di + si), 3);
        DSB((u16)(DSW((u16)(tzn + 2)) + 3)) = 0;
        DSW(RL(daylight)) = 1;
        return;
    }
}

static u16 ydays(int i) { return DSW((u16)(RL(cum_days) + 2 * i)); }

/* INTRO 0000:3602 / PS 0000:44c4 __isDST(hour, yday, month, yr70) */
static bool is_dst(u8 hour, u16 yday, u16 month, s16 yr70)
{
    if (month == 0) {
        u16 si = yday;
        if (yday >= 59 && ((yr70 + 70) & 3) == 0) si--;
        month = 0;
        while (ydays(month) <= si) month++;
    } else {
        yday = (u16)(yday + ydays(month - 1));
        if (month > 3 && ((yr70 + 70) & 3) == 0) yday++;
    }
    if (month < 4 || month > 10) return false;
    if (month != 4 && month != 10) return true;
    u16 cx = (yr70 > 16 && month == 4) ? (u16)(ydays(month - 1) + 7) : ydays(month);
    if ((yr70 + 1970) & 3) cx--;
    u16 bx = (u16)(((s16)(yr70 + 1) >> 2) + cx);
    u16 ax = (u16)(365 * (u16)yr70 + bx + 4);
    cx = (u16)(cx - ax % 7);
    if (month == 4) {
        if (yday > cx) return true;
        if (yday != cx) return false;
        return hour >= 2;
    }
    if (yday < cx) return true;
    if (yday != cx) return false;
    return hour <= 1;
}

/* INTRO 0000:3198 / PS 0000:405a dostounix(date*, time*) */
static s32 dostounix(u16 d, u16 t)
{
    rtl_tzset();
    u32 x = DSL(RL(timezone)) + 0x12CEA600u;
    s16 si = (s16)(DSW(d) - 1980);
    x += (u32)((s32)(si >> 2) * 0x07861F80);
    x += (u32)((s32)(si & 3) * 0x01E13380);
    if (si & 3) x += 0x15180;
    u16 cx = 0;
    for (s16 i = (s16)(DSC(d + 3) - 1); i > 0;) { i--; cx = (u16)(cx + DSC((u16)(RL(month_days) + i))); }
    cx = (u16)(cx + DSC(d + 2) - 1);
    if (DSC(d + 3) > 2 && (DSW(d) & 3) == 0) cx++;
    s16 hours = (s16)(cx * 24 + DSB(t + 1));
    if (DSW(RL(daylight)) && is_dst(DSB(t + 1), cx, 0, (s16)(DSW(d) - 1970))) hours--;
    x += (u32)((s32)hours * 3600);
    x += (u32)((s32)DSB(t) * 60 + DSB(t + 3));
    return (s32)x;
}

/* INTRO 0000:1276 / PS 0000:1f3a time(tloc) */
s32 rtl_time(u16 tloc_ds)
{
    u16 m = stk_mark();
    u16 d = stk_alloc(4), t = stk_alloc(4);
    rtl_getdate(d);
    rtl_gettime(t);
    s32 v = dostounix(d, t);
    stk_release(m);
    if (tloc_ds) DSL(tloc_ds) = (u32)v;
    return v;
}

/* ------------------------------------------------------------------ start-up (c0 _StartTable) */

/* INTRO 0000:1d73 / PS 0000:2a57 _setenvp (priority 16): the environment strings copied to the heap,
 * environ[] after them. */
static void setenvp(void)
{
    u16 n = DSW(RTL_DS_envLng);
    u16 p = rtl_malloc(n);
    if (!p) host_fatal("%s: _setenvp: out of memory", prog_current()->exe);
    for (u16 i = 0; i < n; i++) DSB((u16)(p + i)) = rd8(DSW(RTL_DS_envseg), i);
    u16 arr = rtl_malloc(DSW(RTL_DS_envSize));
    DSW(RL(environ)) = arr;
    if (!arr) host_fatal("%s: _setenvp: out of memory", prog_current()->exe);
    u16 bx = arr, di = p;
    while (DSB(di)) {
        DSW(bx) = di;
        bx = (u16)(bx + 2);
        while (DSB(di)) di++;
        di++;
    }
    DSW(bx) = 0;
    DSW(RTL_DS_environ88) = DSW(RL(environ));
}

/* The #pragma startup routines in priority order (c0 0000:0220 _StartTable), run by prog_run after
 * the c0 set-up and before main. AP: 1790:000d (priority 0) = INT 21h/48h FFFFh (result unused),
 * DS:5BD6 = 1A00h; 36f6:0d29 (1) = the overlay manager (dropped); _setupio (2); _setargv (16, done by
 * prog.c on the stack). INTRO/PS: _setupio (2); _setargv, _setenvp (16); tzset (30). */
void rtl_c0_start(void)
{
    dos_prog_start();
    int id = prog_current_id();
    if (id == PROG_AP) DSW(0x5BD6) = 0x1A00;         /* AP 1790:000d */
    rtl_setupio();
    if (id != PROG_AP) {
        setenvp();
        rtl_tzset();
    }
}

void rtl_c0_exit(void) { rtl_exit_streams(); }
