#pragma once
/* shell_data private helpers (PS.EXE only, DGROUP 4D08). Not part of the module interface. */
#include "shell_data/shell_data.h"
#include "platform/platform.h"
#include "symbols_ps.h"
#include "prog.h"

/* Far data segments holding each source file's name: the first argument of shell_fatal
 * (227c:0143, platform assert_fail) is FarPtr {0, SEG(file segment)}. */
enum {
    FS_FILE = 0x4be1,  FS_FARQUICK = 0x4c81, FS_SNARIO = 0x4c82, FS_HISTORIC = 0x4c83,
    FS_EXTENS = 0x4c84, FS_SHIPTEXT = 0x4c85, FS_PLANETXT = 0x4c86, FS_THREED = 0x4c87,
    FS_QUEUE = 0x4c88, FS_RANKS = 0x4c8a, FS_HELPER = 0x4c8b, FS_WDATA = 0x4c8c,
    FS_WDUTIL = 0x4c90, FS_STRP = 0x4c98, FS_ACE = 0x4c99, FS_SQUAD = 0x4c9a, FS_TEXT = 0x4c9c,
    FS_EMD = 0x4c9d, FS_LIST = 0x4c9e, FS_AIRCRAFT = 0x4c9f, FS_MEDALS = 0x4ca0, FS_DATE = 0x4ca2,
    FS_CAMPAIGN = 0x4ca3, FS_MISSINFO = 0x4ca4, FS_WLDFGEN = 0x4ca5, FS_CONVOY = 0x4ca6,
    FS_SHIP = 0x4ca7, FS_FORM = 0x4ca8, FS_STATION = 0x4ca9, FS_ORDNANCE = 0x4caa,
    FS_SCRIPT = 0x4cff, FS_TABLES = 0x4d00
};

/* shell_fatal(file, line, msg): prints, waits for a key and exits (never returns). */
static inline void sd_fatal_msg(u16 fseg, u16 line, u16 msg_ds)
{
    assert_fail(far_make(SEG(fseg), 0), (s16)line, msg_ds);
}
static inline void sd_fatal(u16 fseg, u16 line) { sd_fatal_msg(fseg, line, 0); }

/* Far memory accessors (offset arithmetic wraps inside the segment, as far pointers do). */
static inline u8  fb(FarPtr p, u16 o) { return rd8(p.seg, (u16)(p.off + o)); }
static inline s8  fc(FarPtr p, u16 o) { return (s8)rd8(p.seg, (u16)(p.off + o)); }
static inline u16 fw(FarPtr p, u16 o) { return rd16(p.seg, (u16)(p.off + o)); }
static inline s16 fs(FarPtr p, u16 o) { return (s16)rd16(p.seg, (u16)(p.off + o)); }
static inline u32 fl(FarPtr p, u16 o) { return rd32(p.seg, (u16)(p.off + o)); }
static inline FarPtr fp(FarPtr p, u16 o) { return far_rd(p.seg, (u16)(p.off + o)); }
static inline void fwb(FarPtr p, u16 o, u8 v)  { wr8(p.seg, (u16)(p.off + o), v); }
static inline void fww(FarPtr p, u16 o, u16 v) { wr16(p.seg, (u16)(p.off + o), v); }
static inline void fwl(FarPtr p, u16 o, u32 v) { wr32(p.seg, (u16)(p.off + o), v); }
static inline void fwp(FarPtr p, u16 o, FarPtr v) { far_wr(p.seg, (u16)(p.off + o), v); }
static inline bool far_eq(FarPtr a, FarPtr b) { return a.off == b.off && a.seg == b.seg; }
static inline FarPtr far_null(void) { FarPtr p = { 0, 0 }; return p; }

/* Near (DGROUP) far pointer variables */
#define DSFP(o) ds_far(o)

/* A C string literal copied onto the emulated DGROUP stack (released with the caller's stk mark). */
static inline u16 sd_lit(const char *s)
{
    u16 n = 0;
    while (s[n]) n++;
    u16 p = stk_alloc((u16)(n + 1));
    for (u16 i = 0; i <= n; i++) DSB(p + i) = (u8)s[i];
    return p;
}

/* AOTP_TEST=missions: rng_below draw trace (host-side dev aid, sd_test.c) */
extern void (*sd_rng_trace)(s16 n, s32 r);

/* Code pointers of this module (file segment << 16 | offset). */
#define SD_FN(seg, off) ((u32)(seg) << 16 | (off))

/* Mission structures (shell_data.md §6) */
#define FLT(side, i) ((u16)(PS_DS_flights + (side) * 0x90 + (i) * 0x24))   /* flight record */
#define CONV(i)      ((u16)(PS_DS_convoys + (i) * 0x14))
#define PLACE(i)     ((u16)(PS_DS_places + (i) * 0x12))
#define HDR          PS_DS_mission_header                                  /* DS:626B */
#define MKIND        (PS_DS_mission_header + 0)
#define MTYPE        (PS_DS_mission_header + 1)
#define MAREA        (PS_DS_mission_header + 2)

/* ---- internal functions shared between files of the module (not in shell_data.h because they are
 * file-local in modules.csv but called by a sibling file, or helpers of the port) */
void ms_open(u16 p, s16 len);
FarPtr keyed_lookup(FarPtr l, u16 keyoff, u8 key, u16 date, u16 end, int *found);
