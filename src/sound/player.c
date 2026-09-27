/* The SCI1 sound player (Sierra's midi.s) — INTRO 1373 / PS 430a / AP 3060, byte-identical in the three
 * programs (PS offsets = INTRO + 1, AP = INTRO + 4) — sound.md §3–§5.
 *
 * State: the player keeps its variables in its own code segment (sound.md §3.4). The port keeps them
 * there too, in mem[] at the current program's player segment, so their initial values come from the
 * loaded image; nodes are the game's 0x17A-byte blocks in mem[] addressed by far pointer (the
 * per-channel tables overlap, the byte layout matters). Addresses in comments are INTRO offsets.
 *
 * Driver calls (lcall cs:[0371] with BP = function) go to the AdLib driver model (adl_driver) with the
 * stored driver pointer. Concurrency: the service runs on the audio thread with the host audio lock
 * held; the entry points the original runs under pushf/cli take the same lock (PORTING.md "Timing").
 * Verified write-for-write against tools/aotpsnd.py (Sequencer) with AOTP_TEST=dro. */
#include "sound/snd_priv.h"

#include "host.h"
#include "prog.h"

/* ---- player variables (cs offsets, sound.md §3.4) */
enum {
    V_LIST = 0x018F, V_DIGLIST = 0x01CF, V_MAP = 0x020F,
    V_TLOCKED = 0x021F, V_TPRIO = 0x022F, V_TVOICES = 0x023F, V_TENTRY = 0x024F,
    V_BACKUP = 0x025F,                         /* 025F..029E: copy of 021F..025E */
    V_OLDOWNER = 0x029F, V_OLDCHAN = 0x02DF, V_PENDVOL = 0x032F, V_RESTORE = 0x033F,
    V_PREPTAB = 0x034E, V_PREPPRIO = 0x036E, V_BUSY = 0x036F, V_DRIVER = 0x0371,
    V_PLAYID = 0x0375, V_CURDEV = 0x0376, V_CURFIXED = 0x0377, V_VOICES = 0x0378, V_DIGITAL = 0x0379,
    V_CURTAG = 0x037A, V_DEFREVERB = 0x037B, V_CHFIRST = 0x037C, V_CHLAST = 0x037E, V_RESBASE = 0x0380,
    V_SAVEDAL = 0x0382, V_REMAP = 0x0383, V_VOLQ = 0x0384, V_VOLRR = 0x0385, V_SEEKING = 0x0388,
};

/* Host-side placement cache (not game state): the player's segment and offset delta. */
static u16 pl_seg, pl_delta;
static u16 test_seg, test_delta;
u32 snd_dbg_restarts;

void player_set_test_base(u16 seg, u16 delta) { test_seg = seg; test_delta = delta; }

static void pl_select(void)
{
    switch (prog_current_id()) {
    case PROG_INTRO: pl_seg = SEG(0x1373); pl_delta = 0; break;
    case PROG_PS:    pl_seg = SEG(0x430A); pl_delta = 1; break;
    case PROG_AP:    pl_seg = SEG(0x3060); pl_delta = 4; break;
    default:         pl_seg = test_seg; pl_delta = test_delta; break;   /* developer test */
    }
}

static inline u8 *pv(u16 o) { return mp(pl_seg, (u16)(o + pl_delta)); }
#define PB(o) (*pv(o))
static inline u16 pw(u16 o) { u8 *p = pv(o); return (u16)(p[0] | p[1] << 8); }
static inline void pw_set(u16 o, u16 v) { u8 *p = pv(o); p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static inline FarPtr pf(u16 o) { FarPtr f = { pw(o), pw((u16)(o + 2)) }; return f; }
static inline void pf_set(u16 o, FarPtr f) { pw_set(o, f.off); pw_set((u16)(o + 2), f.seg); }

static inline FarPtr list_at(int i) { return pf((u16)(V_LIST + 4 * i)); }
static inline void list_set(int i, FarPtr f) { pf_set((u16)(V_LIST + 4 * i), f); }
static inline bool far_eq(FarPtr a, FarPtr b) { return a.off == b.off && a.seg == b.seg; }
static const FarPtr FAR_NULL = { 0, 0 };

/* ---- node access */
static inline u8 *np(FarPtr n, u16 o) { return mp(n.seg, (u16)(n.off + o)); }
#define NB(n, o) (*np(n, (u16)(o)))
static inline u16 nw(FarPtr n, u16 o) { u8 *p = np(n, o); u8 *q = np(n, (u16)(o + 1)); return (u16)(*p | *q << 8); }
static inline void nw_set(FarPtr n, u16 o, u16 v) { *np(n, o) = (u8)v; *np(n, (u16)(o + 1)) = (u8)(v >> 8); }
static inline FarPtr nf(FarPtr n, u16 o) { FarPtr f = { nw(n, o), nw(n, (u16)(o + 2)) }; return f; }

/* resource bytes (the data after the 84 00 header: *handle) */
static inline FarPtr res_of(FarPtr n) { FarPtr h = nf(n, 0x008); return far_rd(h.seg, h.off); }
static inline u8 rb(FarPtr r, u16 o) { return *mp(r.seg, (u16)(r.off + o)); }
static inline u16 rw(FarPtr r, u16 o) { return (u16)(rb(r, o) | rb(r, (u16)(o + 1)) << 8); }
static inline void rb_set(FarPtr r, u16 o, u8 v) { *mp(r.seg, (u16)(r.off + o)) = v; }

/* ---- driver call (lcall cs:[0371], BP = fn) */
static u16 drv(u16 fn, u16 ax, u16 cx)
{
    DrvRegs r = { ax, cx, 0 };
    adl_driver(pf(V_DRIVER), fn, &r);
    return r.ax;
}
static void drv7(u8 d, u8 ctl, u8 val) { drv(7, d, (u16)(ctl << 8 | val)); }

#define LOCK()   host_audio_lock()
#define UNLOCK() host_audio_unlock()

static void p_remap(void);
static void p_sequence(FarPtr n, int idx);

/* 1fb5: node volume scaling (8-bit arithmetic) */
static u8 scale_volume(u8 v, u8 nv)
{
    u16 ax = (u16)((u8)(v + 1) * (u8)(nv + 1));
    u8 c = (u8)((ax >> 8) << 1);
    return c ? (u8)(c - 1) : 0;
}

/* 1fcd: list index of a node, FF if not listed */
static u8 p_find(FarPtr n)
{
    for (int i = 0; i < 16; i++)
        if (far_eq(list_at(i), n)) return (u8)i;
    return 0xFF;
}

/* 1fef: point the resource's header at the device track's channels (in place, once) */
static void p_prepare(FarPtr n)
{
    if (nw(n, 0x008) == 0xFFFF && nw(n, 0x00A) == 0xFFFF) return;
    FarPtr r = res_of(n);
    if (rb(r, 0x21) == 0xFC && rb(r, 0x22) == 0xFD && rb(r, 0x23) == 0xFE) return;
    for (int i = 0; i < 16; i++) pw_set((u16)(V_PREPTAB + 2 * i), 0);
    PB(V_PREPPRIO) = 0xFF;
    u16 p = 0;
    if (rb(r, 0) == 0xF0) { PB(V_PREPPRIO) = rb(r, 1); p = 8; }
    int si = 0;
    for (;;) {
        u8 c = rb(r, p);
        if (c == PB(V_PLAYID)) {
            p++;
            for (;;) {
                c = rb(r, p++);
                if (c == 0xFF) break;
                p++;
                pw_set((u16)(V_PREPTAB + 2 * si), rw(r, p));
                p += 4;
                si++;
            }
            break;
        }
        if (c == 0xFF) break;
        p++;
        for (;;) {
            c = rb(r, p++);
            if (c == 0xFF) break;
            p += 5;
        }
    }
    for (int i = 0; i < 16; i++) {
        u16 t = pw((u16)(V_PREPTAB + 2 * i));
        rb_set(r, (u16)(2 * i), (u8)t);
        rb_set(r, (u16)(2 * i + 1), (u8)(t >> 8));
    }
    rb_set(r, 0x20, PB(V_PREPPRIO));
    rb_set(r, 0x21, 0xFC); rb_set(r, 0x22, 0xFD); rb_set(r, 0x23, 0xFE);
}

/* 0b6a: take a node out of the list */
static void p_unlink(FarPtr n)
{
    u8 i = p_find(n);
    if (i == 0xFF) return;
    for (int k = i; k < 15; k++) list_set(k, list_at(k + 1));
    list_set(15, FAR_NULL);
    NB(n, 0x158) = 0xFF;
    NB(n, 0x159) = 0;
    u8 al = NB(n, 0x165);
    if (al != 0 && al >= 0x80) {                    /* digital sample started: end it (fn 15) */
        FarPtr r = res_of(n);
        u16 bp = (u16)(r.off + rw(r, (u16)((al & 0x0F) * 2 - 2)));
        drv(15, bp, 0);
    }
}

/* 12b3 / 1441: bring device channel d up to date with channel c of node n */
static void setup_channel(u8 d, FarPtr n, u8 c)
{
    drv7(d, 0x7B, 0);
    drv7(d, 0x4B, NB(n, 0x0DA + c) & 0x0F);
    drv(8, d, NB(n, 0x116 + c));
    PB(V_PENDVOL + d) = 0xFF;
    drv7(d, 0x07, scale_volume(NB(n, 0x107 + c), NB(n, 0x15E)));
    drv7(d, 0x0A, NB(n, 0x0F8 + c));
    drv7(d, 0x01, NB(n, 0x0E9 + c));
    u16 w = nw(n, (u16)(0x0BC + 2 * c));
    drv7(d, 0x40, (w & 0x8000) ? 0x7F : 0);
    drv(10, d, (u16)((w & 0x7F) << 8 | ((w >> 7) & 0x7F)));
    drv7(d, 0x4E, NB(n, 0x125 + c));
}

#define T_LOCKED(d) PB(V_TLOCKED + (d))
#define T_PRIO(d)   PB(V_TPRIO + (d))
#define T_VOICES(d) PB(V_TVOICES + (d))
#define T_ENTRY(d)  PB(V_TENTRY + (d))

static void t_free(int d) { T_ENTRY(d) = 0xFF; T_VOICES(d) = 0; T_PRIO(d) = 0; T_LOCKED(d) = 0; }

/* 0f74: free the mapped channel with the strictly largest priority value (first one) -> index, or -1 */
static int t_steal(u8 *voices)
{
    u8 best = 0;
    int dh = -1;
    for (int d = 0; d < 16; d++)
        if (best < T_PRIO(d)) { best = T_PRIO(d); dh = d; }
    if (dh < 0) return -1;
    *voices = T_VOICES(dh);
    t_free(dh);
    return dh;
}

/* 0c1e: distribute the device channels and the driver voices over the playing sounds */
static void p_remap(void)
{
    pw_set(V_BUSY, (u16)(pw(V_BUSY) + 1));
    PB(V_REMAP) = 0;
    u8 newmap[16];
    for (int d = 0; d < 16; d++) { newmap[d] = 0xFF; T_ENTRY(d) = 0xFF; T_LOCKED(d) = T_PRIO(d) = T_VOICES(d) = 0; }
    pf_set(V_DIGLIST, FAR_NULL);
    u16 chf = pw(V_CHFIRST), chl = pw(V_CHLAST);
    if (!far_is_null(list_at(0))) {
        u8 cl = NB(list_at(0), 0x15F);
        if (cl == 0x7F) cl = PB(V_DEFREVERB);
        drv(11, 0, cl);
        u8 al = PB(V_VOICES);
        for (int idx = 0; idx < 16; idx++) {
            FarPtr node = list_at(idx);
            if (far_is_null(node)) break;
            if (NB(node, 0x164)) continue;                          /* paused */
            if (NB(node, 0x165)) {                                  /* digital */
                if (far_is_null(pf(V_DIGLIST))) pf_set(V_DIGLIST, node);
                continue;
            }
            for (int i = 0; i < 0x40; i++) PB(V_BACKUP + i) = PB(V_TLOCKED + i);
            PB(V_SAVEDAL) = al;
            bool rollback = false;
            for (int di = 0; di < 16 && !rollback; di++) {
                u8 c = NB(node, 0x08C + di);
                if (c == 0xFF || c == 0xFE || c == 0x0F) continue;
                if ((NB(node, 0x134 + c) & 2) || NB(node, 0x143 + c)) continue;   /* fixed or muted */
                u8 dl = (u8)(idx << 4 | c);
                u8 ah = NB(node, 0x0DA + c) & 0x0F;
                u8 ch = NB(node, 0x0DA + c) >> 4;
                if (ch) ch = (u8)(0x10 - ch + idx * 16);
                int dh = -1;
                if ((NB(node, 0x134 + c) & 1) && T_ENTRY(c) == 0xFF) {
                    dh = c;                                                /* locked: own number */
                } else {
                    bool skip = false;
                    for (u16 d = 0; d < 16; d++) {
                        if (T_ENTRY(d) == 0xFF) {
                            if (chf <= d && d <= chl) dh = d;
                        } else if (T_ENTRY(d) == dl) { skip = true; break; }
                    }
                    if (skip) continue;
                    if (dh < 0) {
                        if (ch) break;                                     /* 0f71: rest skipped */
                        u8 v;
                        dh = t_steal(&v);
                        if (dh < 0) { rollback = true; break; }
                        al = (u8)(al + v);
                    }
                }
                if (ah > al) {                                             /* 0fc8 */
                    if (ch) continue;
                    while (ah > al) {
                        u8 v;
                        if (t_steal(&v) < 0) { rollback = true; break; }
                        al = (u8)(al + v);
                    }
                    if (rollback) break;
                }
                T_ENTRY(dh) = dl; T_VOICES(dh) = ah; al = (u8)(al - ah); T_PRIO(dh) = ch;
                if (!(NB(node, 0x134 + c) & 1)) { T_LOCKED(dh) = 0; continue; }
                T_LOCKED(dh) = 1;
                if (dh == c) continue;
                if (T_LOCKED(c) == 0) {                                    /* swap dh and c */
                    u8 t;
                    t = T_ENTRY(dh); T_ENTRY(dh) = T_ENTRY(c); T_ENTRY(c) = t;
                    t = T_PRIO(dh); T_PRIO(dh) = T_PRIO(c); T_PRIO(c) = t;
                    t = T_VOICES(dh); T_VOICES(dh) = T_VOICES(c); T_VOICES(c) = t;
                    t = T_LOCKED(dh); T_LOCKED(dh) = T_LOCKED(c); T_LOCKED(c) = t;
                    continue;
                }
                if (ch) { t_free(dh); al = (u8)(al + ah); continue; }
                if (T_PRIO(c) == 0) { rollback = true; break; }
                al = (u8)(al + T_VOICES(c));
                t_free(dh);
                T_ENTRY(c) = dl; T_PRIO(c) = ch; T_VOICES(c) = ah;
                al = (u8)(al - ah);
            }
            if (rollback) {
                for (int i = 0; i < 0x40; i++) PB(V_TLOCKED + i) = PB(V_BACKUP + i);
                al = PB(V_SAVEDAL);
            }
        }
        /* phase 2 (1258): locked channels, and channels that keep their device channel */
        for (int si = 0; si < 16; si++) {
            if (T_ENTRY(si) == 0xFF) continue;
            u8 e = T_ENTRY(si);
            FarPtr node = list_at(e >> 4);
            u8 c = e & 0x0F;
            if (T_LOCKED(si)) {
                T_ENTRY(si) = 0xFF;
                newmap[si] = e;
                if (!(PB(V_OLDCHAN + si) == c && far_eq(pf((u16)(V_OLDOWNER + 4 * si)), node)))
                    setup_channel((u8)si, node, c);
            } else {
                u16 d = chf;
                for (;;) {
                    if (far_eq(pf((u16)(V_OLDOWNER + 4 * d)), node) && PB(V_OLDCHAN + d) == c) {
                        if (T_LOCKED(d) == 0) { newmap[d] = e; T_ENTRY(si) = 0xFF; }
                        break;
                    }
                    if (d == chl) break;
                    d++;
                }
            }
        }
        /* phase 3 (1402): the rest from the top free device channel downwards */
        int di = chl + 1;
        for (int si = 0; si < 16; si++) {
            if (T_ENTRY(si) == 0xFF) continue;
            int bx = di - 1;
            while (newmap[bx & 15] != 0xFF) bx--;
            di = bx;
            u8 e = T_ENTRY(si);
            newmap[di & 15] = e;
            setup_channel((u8)di, list_at(e >> 4), e & 0x0F);
        }
    }
    /* finish (1514) */
    for (int d = 15; d >= 0; d--) {
        if (PB(V_OLDCHAN + d) != 0x0F && newmap[d] == 0xFF) {
            drv7((u8)d, 0x40, 0);
            drv7((u8)d, 0x7B, 0);
            drv7((u8)d, 0x4B, 0);
        }
    }
    for (int d = 0; d < 16; d++) {
        PB(V_MAP + d) = newmap[d];
        PB(V_OLDCHAN + d) = newmap[d] & 0x0F;
        pf_set((u16)(V_OLDOWNER + 4 * d), newmap[d] == 0xFF ? FAR_NULL : list_at(newmap[d] >> 4));
    }
    pw_set(V_BUSY, (u16)(pw(V_BUSY) - 1));
}

/* 0528: play(node, locked) */
static void p_play(FarPtr n, u16 locked)
{
    if (p_find(n) != 0xFF) { p_unlink(n); p_remap(); }             /* restart */
    NB(n, 0x159) = locked ? 2 : 1;
    p_prepare(n);
    for (int si = 0; si < 15; si++) {
        nw_set(n, (u16)(0x0C + 2 * si), 0x0D); nw_set(n, (u16)(0x2C + 2 * si), 3);
        nw_set(n, (u16)(0x4C + 2 * si), 0); nw_set(n, (u16)(0x6C + 2 * si), 0);
        nw_set(n, (u16)(0xBC + 2 * si), 0x2000);
        NB(n, 0x08C + si) = 0xFF; NB(n, 0x09C + si) = 0; NB(n, 0x0AC + si) = 0;
        NB(n, 0x0DA + si) = 0xFF; NB(n, 0x0E9 + si) = 0; NB(n, 0x116 + si) = 0xFF;
        NB(n, 0x107 + si) = 0xFF; NB(n, 0x0F8 + si) = 0xFF; NB(n, 0x125 + si) = 0xFF;
        NB(n, 0x134 + si) = 0; NB(n, 0x143 + si) = 0;
    }
    NB(n, 0x08C + 15) = 0xFF; NB(n, 0x09C + 15) = 0; NB(n, 0x0AC + 15) = 0;
    NB(n, 0x165) = 0; NB(n, 0x15A) = 0; NB(n, 0x15F) = 0x7F;
    nw_set(n, 0x0C + 30, 0x0D); nw_set(n, 0x2C + 30, 3); nw_set(n, 0x4C + 30, 0);
    nw_set(n, 0x156, 0);
    FarPtr r = res_of(n);
    if (rb(r, 0x20) != 0xFF && NB(n, 0x15B) == 0) NB(n, 0x15C) = rb(r, 0x20);
    for (int si = 0; si < 16; si++) {
        u16 off = rw(r, (u16)(2 * si));
        if (off == 0) break;
        u8 dl = rb(r, off);
        if (dl == 0xFE) {                                          /* digital channel */
            if (PB(V_DIGITAL) == 0) {
                nw_set(n, (u16)(0x0C + 2 * si), 0); nw_set(n, (u16)(0x2C + 2 * si), 0);
                NB(n, 0x08C + si) = 0xFE;
                continue;
            }
            NB(n, 0x165) = (u8)(si + 1);
            break;
        }
        NB(n, 0x08C + si) = dl;
        NB(n, 0x09C + si) = dl | 0xB0;                             /* running status: controller */
        u8 d = rb(r, (u16)(off + 0x0C));
        nw_set(n, (u16)(0x4C + 2 * si), d == 0xF8 ? 0x80F0 : d);
        NB(n, 0x08C + si) = dl & 0x0F;
        u8 c = dl & 0x0F;
        if (dl & 0x10) {                                           /* fixed: events from +3 */
            nw_set(n, (u16)(0x0C + 2 * si), 3); nw_set(n, (u16)(0x4C + 2 * si), 0);
            NB(n, 0x134 + c) |= 2;
            continue;
        }
        if (dl & 0x20) NB(n, 0x134 + c) |= 1;                      /* locked */
        if (dl & 0x40) NB(n, 0x143 + c) = 1;                       /* starts muted */
        if (c == 15) {
            if (NB(n, 0x15F) == 0x7F) { NB(n, 0x15F) = rb(r, (u16)(off + 8)); continue; }
        } else {
            if (NB(n, 0x0DA + c) == 0xFF) NB(n, 0x0DA + c) = rb(r, (u16)(off + 1));
            if (NB(n, 0x116 + c) == 0xFF) NB(n, 0x116 + c) = rb(r, (u16)(off + 4));
            if (NB(n, 0x107 + c) == 0xFF) NB(n, 0x107 + c) = rb(r, (u16)(off + 8));
        }
        if (NB(n, 0x0F8 + c) == 0xFF) NB(n, 0x0F8 + c) = rb(r, (u16)(off + 0x0B));
    }
    if (NB(n, 0x159) == 2)
        for (int si = 0; si < 15; si++) NB(n, 0x134 + si) |= 1;   /* every channel locked */
    /* insert: first position whose node has priority <= ours; later entries shift down */
    u8 dl = NB(n, 0x15C);
    int di;
    for (di = 0; di < 16; di++) {
        FarPtr o = list_at(di);
        if (far_is_null(o) || NB(o, 0x15C) <= dl) {
            for (int i = 14; i >= di; i--) list_set(i + 1, list_at(i));
            list_set(di, n);
            break;
        }
    }
    if (di == 16) return;                                          /* list full */
    if (PB(V_SEEKING)) return;
    nw_set(n, 0x152, 0); nw_set(n, 0x154, 0); NB(n, 0x158) = 0;
    NB(n, 0x160) = NB(n, 0x161) = NB(n, 0x162) = NB(n, 0x163) = NB(n, 0x164) = 0;
    p_remap();
}

/* 16b8: node volume; queued (fades) or sent at once */
static void p_set_node_volume(FarPtr n, u8 vol, bool queue)
{
    if (vol == NB(n, 0x15E)) return;
    NB(n, 0x15E) = vol;
    u8 idx = p_find(n);
    if (idx == 0xFF) return;
    for (int d = 0; d < 16; d++) {
        u8 e = PB(V_MAP + d);
        if (e != 0xFF && (e & 0xF0) == (u8)(idx << 4)) {
            u8 v = scale_volume(NB(n, 0x107 + (e & 0x0F)), vol);
            if (queue) PB(V_PENDVOL + d) = v;
            else { PB(V_PENDVOL + d) = 0xFF; drv7((u8)d, 7, v); }
        }
    }
    for (int si = 0; si < 16; si++) {
        u8 c = NB(n, 0x08C + si);
        if (c == 0xFF) break;
        if ((NB(n, 0x134 + c) & 2) && PB(V_MAP + (c & 15)) == 0xFF) {
            u8 v = scale_volume(NB(n, 0x107 + c), vol);
            if (queue) PB(V_PENDVOL + (c & 15)) = v;
            else { PB(V_PENDVOL + (c & 15)) = 0xFF; drv7(c, 7, v); }
        }
    }
}

/* 15fc: one fade step (from the service) */
static void p_fade_step(FarPtr n)
{
    if (NB(n, 0x162)) { NB(n, 0x162)--; return; }
    NB(n, 0x162) = NB(n, 0x161);
    u8 tgt = NB(n, 0x160) & 0x7F, vol = NB(n, 0x15E), st = NB(n, 0x163);
    if (tgt < vol) {
        if ((u8)(vol - tgt) <= st) p_set_node_volume(n, tgt, true);
        else { p_set_node_volume(n, (u8)(vol - st), true); return; }
    } else if (tgt > vol) {
        if ((u8)(tgt - vol) <= st) p_set_node_volume(n, tgt, true);
        else { p_set_node_volume(n, (u8)(vol + st), true); return; }
    }
    NB(n, 0x163) = 0;
    if (NB(n, 0x160) & 0x80) { p_unlink(n); PB(V_REMAP) = 1; }
}

/* 1796: at most 2 queued volume changes per tick, round robin */
static void p_send_pending_volumes(void)
{
    int bl = 0;
    u8 rr = PB(V_VOLRR) & 15;
    u8 si = rr;
    for (;;) {
        u8 v = PB(V_PENDVOL + si);
        if (v != 0xFF) {
            PB(V_PENDVOL + si) = 0xFF;
            drv7(si, 7, v);
            if (++bl == 2) break;
        }
        si = (u8)((si + 1) & 15);
        if (si == rr) break;
    }
    PB(V_VOLRR) = si;
}

/* 1e66: status nibble F (control channel) */
static void ev_control(FarPtr n, int si, u8 hi, FarPtr r, u16 base)
{
    u16 po = (u16)(0x0C + 2 * si);
#define RD() (nw_set(n, po, (u16)(nw(n, po) + 1)), rb(r, (u16)(base + nw(n, po) - 1)))
    if (hi == 0xC0) {
        u8 v = RD();
        if (v == 0x7F) {                                           /* loop point */
            u8 d = RD();
            nw_set(n, (u16)(0x4C + 2 * si), d == 0xF8 ? 0x80F0 : d);
            NB(n, 0x09C + si) = 0xCF;
            for (int i = 0; i < 16; i++) {
                nw_set(n, (u16)(0x2C + 2 * i), nw(n, (u16)(0x0C + 2 * i)));
                nw_set(n, (u16)(0x6C + 2 * i), nw(n, (u16)(0x4C + 2 * i)));
                NB(n, 0x0AC + i) = NB(n, 0x09C + i);
            }
            nw_set(n, 0x156, nw(n, 0x154));
            nw_set(n, po, (u16)(nw(n, po) - 1));
            nw_set(n, (u16)(0x4C + 2 * si), 0);
        } else if (!PB(V_SEEKING)) {
            NB(n, 0x158) = v;                                      /* signal to the game */
        }
    } else if (hi == 0xB0) {
        u8 ctl = RD(), val = RD();
        if (ctl == 0x50) {                                         /* reverb */
            if (val == 0x7F) val = PB(V_DEFREVERB);
            NB(n, 0x15F) = val;
            drv(11, 0, val);
        } else if (ctl == 0x60) {                                  /* cue */
            if (!PB(V_SEEKING)) nw_set(n, 0x152, (u16)(nw(n, 0x152) + 1));
        } else if (ctl == 0x52) {                                  /* hold point */
            if (NB(n, 0x15A) == val)
                for (int i = 0; i < 16; i++) nw_set(n, (u16)(0x0C + 2 * i), 0);
        }
    } else if (hi == 0xF0) {
        while (RD() != 0xF7) {}
    } else {                                                       /* 1f78 ev_skip_control */
        if (hi != 0xC0 && hi != 0xD0) (void)RD();
        (void)RD();
    }
#undef RD
}

/* 1b76 / 1bc8 / 1c73 / 1d72 / 1de8 ...: the events of one channel in this tick */
static void p_events(FarPtr n, int si, u8 c, u8 dev, u8 fixed, FarPtr r, u16 base)
{
    u16 po = (u16)(0x0C + 2 * si);
#define RD() (nw_set(n, po, (u16)(nw(n, po) + 1)), rb(r, (u16)(base + nw(n, po) - 1)))
    u8 seeking = PB(V_SEEKING);
    for (;;) {
        u8 st = RD();
        if (st < 0x80) { st = NB(n, 0x09C + si); nw_set(n, po, (u16)(nw(n, po) - 1)); }
        else NB(n, 0x09C + si) = st;
        u8 hi = st & 0xF0, lo = st & 0x0F;
        if (st == 0xFC) { nw_set(n, po, 0); return; }             /* channel end */
        if (lo == 0x0F) {
            ev_control(n, si, hi, r, base);
            seeking = PB(V_SEEKING);
            if (nw(n, po) == 0) return;
        } else {
            u8 cc = c & 0x0F;
            bool blocked = fixed && PB(V_MAP + (dev & 0x0F)) != 0xFF;
            switch (hi) {
            case 0x80: case 0x90: {                                /* ev_note_off / ev_note_on */
                u8 note = RD(), vel = RD();
                if (hi == 0x90 && vel) {
                    if (dev < 0x10) NB(n, 0x125 + cc) = note;
                    if (dev != 0xFF && !seeking) drv(5, dev & 0x0F, (u16)(note << 8 | vel));
                } else {
                    if (dev < 0x10 && NB(n, 0x125 + cc) == note) NB(n, 0x125 + cc) = 0xFF;
                    if (dev != 0xFF && !seeking) drv(4, dev & 0x0F, (u16)(note << 8 | vel));
                }
                break;
            }
            case 0xA0: {                                           /* ev_poly_at */
                u8 a = RD(), b = RD();
                if (dev != 0xFF && !seeking) drv(6, dev, (u16)(a << 8 | b));
                break;
            }
            case 0xB0: {                                           /* ev_controller */
                u8 ctl = RD(), val = RD();
                if (blocked) break;
                bool send = true;
                switch (ctl) {
                case 0x07:
                    NB(n, 0x107 + cc) = val;
                    val = scale_volume(val, NB(n, 0x15E));
                    if (dev < 0x20) PB(V_PENDVOL + (dev & 15)) = 0xFF;
                    else send = false;
                    break;
                case 0x0A: NB(n, 0x0F8 + cc) = val; break;
                case 0x01: NB(n, 0x0E9 + cc) = val; break;
                case 0x40: {
                    u16 w = nw(n, (u16)(0xBC + 2 * cc));
                    nw_set(n, (u16)(0xBC + 2 * cc), val ? (u16)(w | 0x8000) : (u16)(w & 0x7FFF));
                    break;
                }
                case 0x4B:
                    NB(n, 0x0DA + cc) = (u8)((NB(n, 0x0DA + cc) & 0xF0) | val);
                    PB(V_REMAP) = 1;
                    break;
                case 0x4E:
                    NB(n, 0x143 + cc) = (u8)((NB(n, 0x143 + cc) & 0xF0) | (val ? 1 : 0));
                    PB(V_REMAP) = 1;
                    break;
                }
                if (send && dev != 0xFF && !seeking) drv7(dev & 0x0F, ctl, val);
                break;
            }
            case 0xC0: {                                           /* ev_program */
                u8 pr = RD();
                if (blocked) break;
                NB(n, 0x116 + cc) = pr;
                if (dev != 0xFF && !seeking) drv(8, dev & 0x0F, pr);
                break;
            }
            case 0xD0: {                                           /* ev_chan_at */
                u8 a = RD();
                if (dev != 0xFF && !seeking) drv(9, dev, a);
                break;
            }
            case 0xE0: {                                           /* ev_pitch_bend */
                u8 lsb = RD(), msb = RD();
                if (blocked) break;
                u16 w = (u16)((msb << 7 | lsb) | (nw(n, (u16)(0xBC + 2 * cc)) & 0x8000));
                nw_set(n, (u16)(0xBC + 2 * cc), w);
                if (dev != 0xFF && !seeking) drv(10, dev & 0x0F, (u16)(lsb << 8 | msb));
                break;
            }
            case 0xF0:                                             /* ev_sysex */
                while (RD() != 0xF7) {}
                break;
            default:
                nw_set(n, po, 0);
                return;
            }
        }
        u8 d = RD();
        if (d == 0) continue;                                      /* next event, same tick */
        nw_set(n, (u16)(0x4C + 2 * si), d == 0xF8 ? 0x80EF : (u16)(d - 1));
        return;
    }
#undef RD
}

/* 1932: one tick of one node */
static void p_sequence(FarPtr n, int idx)
{
    PB(V_CURTAG) = (u8)(idx << 4);
    nw_set(n, 0x154, (u16)(nw(n, 0x154) + 1));
    FarPtr r = res_of(n);
    pw_set(V_RESBASE, r.off);
    for (int si = 0; si < 16; si++) {
        u8 c = NB(n, 0x08C + si);
        if (c == 0xFF) break;
        if (c == 0xFE) continue;
        u8 dev = 0xFF, fixed = 0;
        if (NB(n, 0x134 + c) & 2) { dev = c; fixed = 1; }
        else {
            u8 look = (u8)((c & 0x0F) | PB(V_CURTAG));
            for (int d = 0; d < 16; d++)
                if (PB(V_MAP + d) == look) { dev = (u8)d; break; }
        }
        PB(V_CURDEV) = dev;
        PB(V_CURFIXED) = fixed;
        u16 pos = nw(n, (u16)(0x0C + 2 * si));
        if (pos == 0) continue;
        u16 base = rw(r, (u16)(2 * si));
        u16 dly = nw(n, (u16)(0x4C + 2 * si));
        if (dly) {
            dly--;
            if (dly == 0x8000) {                                   /* end of an F8 run */
                u8 d = rb(r, (u16)(base + pos));
                nw_set(n, (u16)(0x0C + 2 * si), (u16)(pos + 1));
                dly = d == 0xF8 ? 0x80F0 : d;
            }
            nw_set(n, (u16)(0x4C + 2 * si), dly);
            continue;
        }
        p_events(n, si, c, dev, fixed, r, base);
    }
    /* 1b03: the sound ends when every channel has ended */
    for (int si = 0; si < 16; si++) {
        if (NB(n, 0x08C + si) == 0xFF) break;
        if (nw(n, (u16)(0x0C + 2 * si))) return;
    }
    if (NB(n, 0x15A) == 0 && NB(n, 0x15D) == 0) {
        p_unlink(n);
        PB(V_REMAP) = 1;
        return;
    }
    snd_dbg_restarts++;
    nw_set(n, 0x154, nw(n, 0x156));
    for (int si = 0; si < 16; si++) {
        nw_set(n, (u16)(0x0C + 2 * si), nw(n, (u16)(0x2C + 2 * si)));
        nw_set(n, (u16)(0x4C + 2 * si), nw(n, (u16)(0x6C + 2 * si)));
        NB(n, 0x09C + si) = NB(n, 0x0AC + si);
    }
}

/* 1865 / 1893: digital-sample nodes. PORT: never reached with the AdLib driver (fn 0 reports no
 * digital support, so FE channels are disabled at play and 165h stays 0); not ported. */
static void p_dig_check(FarPtr n) { (void)n; }
static void p_dig_service(void) {}

/* ==================================================================================== far stubs */

/* 1373:0003 / 430a:0004 / 3060:0007 -> 09f1 p_cue */
u16 snd_get_cue(FarPtr n) { return nw(n, 0x152); }

/* 1373:0016 / 430a:0017 / 3060:001a -> 091e p_set_priority */
void snd_node_set_priority(FarPtr n, u16 p)
{
    LOCK();
    u8 cl = (u8)p;
    if (NB(n, 0x15C) != cl) {
        NB(n, 0x15C) = cl;
        u8 si = p_find(n);
        if (si != 0xFF) {
            for (int k = si; k < 15; k++) list_set(k, list_at(k + 1));
            list_set(15, FAR_NULL);
            int di = 0;
            while (list_at(di).seg != 0 && NB(list_at(di), 0x15C) > cl) di++;   /* only the segment is tested */
            for (int k = 14; k >= di; k--) list_set(k + 1, list_at(k));
            list_set(di, n);
            p_remap();
        }
    }
    UNLOCK();
}

/* 1373:002a / 430a:002b / 3060:002e -> 09fb p_position: AX = CH seconds, CL half-ticks; DX = minutes */
u16 snd_get_position(FarPtr n, u16 *dx)
{
    u16 t = nw(n, 0x154);
    u16 r = t % 3600;
    if (dx) *dx = t / 3600;
    return (u16)((r / 60) << 8 | ((r % 60) >> 1));
}

/* 1373:003f / 430a:0040 / 3060:0043 -> 0433 p_restore: seek to the saved tick (no caller) */
void snd_node_restore(FarPtr n)
{
    LOCK();
    for (int si = 0; si < 15; si++) PB(V_RESTORE + si) = NB(n, 0x143 + si) & 0xF0;
    PB(V_SEEKING) = 1;
    p_play(n, NB(n, 0x159));              /* quirk: 1 or 2 both play locked */
    u16 cx = nw(n, 0x154);
    nw_set(n, 0x154, 0);
    u8 al = NB(n, 0x15D);
    NB(n, 0x15D) = 1;
    /* PORT: the original passes the caller's DI as the list index (garbage); the port passes the
     * node's index. Only the device lookup is affected, and nothing is sent while seeking. */
    int idx = p_find(n);
    if (cx != 0) {
        for (;;) {
            u16 dxv = nw(n, 0x154);
            p_sequence(n, idx == 0xFF ? 0 : idx);
            u16 now = nw(n, 0x154);
            if (dxv < now) { if (now == cx) break; continue; }
            if (dxv == now) break;
            cx = (u16)(cx - (u16)(dxv - now));
            if (now == cx) break;
        }
    }
    NB(n, 0x15D) = al;
    PB(V_SEEKING) = 0;
    for (int si = 0; si < 15; si++) NB(n, 0x143 + si) |= PB(V_RESTORE + si);
    p_remap();
    UNLOCK();
}

/* 1373:0050 / 430a:0051 / 3060:0054 -> 04c3 p_master_volume: AL = previous */
u16 snd_player_master_volume(u16 v)
{
    LOCK();
    u8 cl = (u8)v;
    if (cl != 0xFF && cl > 15) cl = 15;
    u16 r = drv(12, 0, cl);
    UNLOCK();
    return r;
}

/* 1373:0061 / 430a:0062 / 3060:0065 -> 0816 p_pause (node 0 = every listed node) */
void snd_node_pause(FarPtr n, u16 on)
{
    LOCK();
    u8 cl = (u8)on;
    if (far_is_null(n)) {
        for (int i = 0; i < 16; i++) {
            FarPtr o = list_at(i);
            if (far_is_null(o)) {
                if (i != 0) break;
                continue;                  /* quirk: an empty entry 0 is skipped */
            }
            u8 al = NB(o, 0x164);
            if (cl) al++;
            else if (al) al--;
            NB(o, 0x164) = al;
        }
    } else {
        u8 al = NB(n, 0x164);
        if (cl) al++;
        else if (al) al--;
        NB(n, 0x164) = al;
    }
    p_remap();
    UNLOCK();
}

/* 1373:0075 / 430a:0076 / 3060:0079 -> 0398 p_attach: AL = patch number (FF none) */
u16 player_attach(FarPtr drvp)
{
    LOCK();
    pl_select();
    pf_set(V_DRIVER, drvp);
    DrvRegs r = { 0, 0, 0 };
    adl_driver(drvp, 0, &r);
    PB(V_VOICES) = (u8)r.cx;
    PB(V_PLAYID) = (u8)(r.cx >> 8);
    PB(V_DIGITAL) = (u8)(((r.ax >> 8) & 0xF0) >> 4);
    UNLOCK();
    return r.ax;
}

/* 1373:0086 / 430a:0087 / 3060:008a -> 03c8 p_init: AX (FFFF = failure) */
u16 player_init(FarPtr patch)
{
    LOCK();
    DrvRegs r = { patch.off, 0, patch.seg };
    adl_driver(pf(V_DRIVER), 1, &r);
    PB(V_CHFIRST) = (u8)r.cx;              /* byte writes into the word variables */
    PB(V_CHLAST) = (u8)(r.cx >> 8);
    drv(11, 0, 0);
    UNLOCK();
    return r.ax;
}

/* 1373:009b / 430a:009c / 3060:009f -> 0528 p_play */
void snd_node_play(FarPtr n, u16 locked)
{
    LOCK();
    p_play(n, locked);
    UNLOCK();
}

/* 1373:00af / 430a:00b0 / 3060:00b3 -> 087e p_fade(DL target, CL ticks, CH step) */
void snd_node_fade(FarPtr n, u16 target, u16 ticks, u16 step)
{
    LOCK();
    if (NB(n, 0x15E) != (u8)target) {     /* the full byte: a stop fade (bit 7) always starts */
        NB(n, 0x160) = (u8)target;
        NB(n, 0x161) = (u8)ticks;
        NB(n, 0x163) = (u8)step;
        NB(n, 0x162) = 0;
    }
    UNLOCK();
}

/* 1373:00c9 / 430a:00ca / 3060:00cd -> 0428 p_sound_on: AL = previous */
u16 snd_sound_on(u16 on)
{
    LOCK();
    u16 r = drv(13, 0, on);
    UNLOCK();
    return r;
}

/* 1373:00da / 430a:00db / 3060:00de -> 090e p_volume (not queued) */
void snd_node_volume(FarPtr n, u16 v)
{
    LOCK();
    p_set_node_volume(n, (u8)v, false);
    UNLOCK();
}

/* 1373:00ee / 430a:00ef / 3060:00f2 -> 0b04 p_pitch_bend(CX value14, DL chan) — no cli */
void snd_node_pitch_bend(FarPtr n, u16 value, u16 chan)
{
    LOCK();                               /* PORT: the audio lock stands in for the missing cli */
    u8 si = p_find(n);
    if (si != 0xFF) {
        u8 c = (u8)chan;
        u16 ax = value;
        if (NB(n, 0x0BD + 2 * c) >= 0x80) ax |= 0x8000;        /* sustain bit kept */
        nw_set(n, (u16)(0x0BC + 2 * c), ax);
        u8 dl = (u8)(si << 4 | c);
        for (int d = 0; d < 16; d++) {
            if (PB(V_MAP + d) == dl) {
                drv(10, (u16)d, (u16)((value & 0x7F) << 8 | ((value >> 7) & 0xFF)));
                break;
            }
        }
    }
    UNLOCK();
}

/* 1373:0105 / 430a:0106 / 3060:0109 -> 080c p_stop_remap */
void snd_node_stop(FarPtr n)
{
    LOCK();
    p_unlink(n);
    p_remap();
    UNLOCK();
}

/* 1373:0116 / 430a:0117 / 3060:011a -> 08a2 p_hold */
void snd_node_hold(FarPtr n, u16 v) { NB(n, 0x15A) = (u8)v; }

/* 1373:012a / 430a:012b / 3060:012e -> 09e7 p_signal: 0-FF (FF = not playing) */
u16 snd_node_signal(FarPtr n) { return NB(n, 0x158); }

/* 1373:013f / 430a:0140 / 3060:0143 -> 04da p_default_reverb: AL = previous */
u16 snd_default_reverb(u16 v)
{
    LOCK();
    u8 cl = (u8)v;
    u16 r;
    if (cl == 0xFF) r = drv(11, 0, 0xFF);
    else if (cl > 10) r = PB(V_DEFREVERB);
    else {
        r = PB(V_DEFREVERB);
        PB(V_DEFREVERB) = cl;
        FarPtr l0 = list_at(0);
        if (!far_is_null(l0) && NB(l0, 0x15F) == 0x7F) drv(11, 0, cl);
    }
    UNLOCK();
    return r;
}

/* 1373:0150 / 430a:0151 / 3060:0154 -> 0a1c p_controller(CH ctl, CL val, DL chan) */
void snd_node_controller(FarPtr n, u16 ctl16, u16 val16, u16 chan16)
{
    LOCK();
    u8 si = p_find(n);
    if (si != 0xFF) {
        u8 ctl = (u8)ctl16, val = (u8)val16, c = (u8)chan16;
        bool send = true;
        switch (ctl) {
        case 0x07:
            NB(n, 0x107 + c) = val;
            val = scale_volume(val, NB(n, 0x15E));
            break;
        case 0x0A: NB(n, 0x0F8 + c) = val; break;
        case 0x01: NB(n, 0x0E9 + c) = val; break;
        case 0x40: {
            u16 w = (u16)(nw(n, (u16)(0x0BC + 2 * c)) & 0x7FFF);
            if (val) w |= 0x8000;
            nw_set(n, (u16)(0x0BC + 2 * c), w);
            break;
        }
        case 0x4E: {                      /* the high nibble (mute count), then remap, nothing sent */
            u8 dl = NB(n, 0x143 + c), dh = dl & 0xF0;
            dl &= 0x0F;
            if (val == 0) { if (dh != 0) NB(n, 0x143 + c) = (u8)(dl | (u8)(dh - 0x10)); }
            else if (dh != 0xF0) NB(n, 0x143 + c) = (u8)(dl | (u8)(dh + 0x10));
            p_remap();
            send = false;
            break;
        }
        case 0x7F: NB(n, 0x116 + c) = val; break;                 /* program change */
        }
        if (send) {
            u8 dl = (u8)(si << 4 | c);
            for (int d = 0; d < 16; d++) {
                if (PB(V_MAP + d) == dl) {
                    if (ctl == 0x7F) drv(8, (u16)d, val);
                    else drv7((u8)d, ctl, val);
                    break;
                }
            }
        }
    }
    UNLOCK();
}

/* 1373:016a / 430a:016b / 3060:016e -> 03ed p_terminate: fn 12 (15), fn 2 */
void player_terminate(void)
{
    LOCK();
    drv(12, 0, 15);
    drv(2, 0, 0);
    UNLOCK();
}

/* 1373:017b / 430a:017c / 3060:017f -> 08ac p_mute_all */
void snd_node_mute_all(FarPtr n, u16 mute)
{
    LOCK();
    pw_set(V_BUSY, (u16)(pw(V_BUSY) + 1));
    for (int si = 0; si < 15; si++) {
        u8 dl = NB(n, 0x143 + si), dh = dl & 0xF0;
        dl &= 0x0F;
        if (mute == 0) { if (dh) dh = (u8)(dh - 0x10); }
        else if (dh != 0xF0) dh = (u8)(dh + 0x10);
        NB(n, 0x143 + si) = (u8)(dl | dh);
    }
    p_remap();
    pw_set(V_BUSY, (u16)(pw(V_BUSY) - 1));
    UNLOCK();
}

/* 1373:040c p_lock (CX = 0 lock, else unlock). No caller; not reached from any stub. */
void p_lock(u16 cx)
{
    if (cx == 0) pw_set(V_BUSY, (u16)(pw(V_BUSY) + 1));
    else if (pw(V_BUSY)) pw_set(V_BUSY, (u16)(pw(V_BUSY) - 1));
}

/* 1373:17de / 430a:17df / 3060:17e2 — the 59.17 Hz service. PORT: runs on the audio thread (host
 * sound service, lock held); the timer client slot registered by snd_init / sx_init is a no-op. */
void player_service(void)
{
    if (!pl_seg) return;
    LOCK();
    if (pw(V_BUSY) == 0) {
        if (PB(V_REMAP)) p_remap();
        int si = 0;
        while (si < 16) {
            FarPtr n = list_at(si);
            if (far_is_null(n)) break;
            if (NB(n, 0x164) == 0) {
                if (NB(n, 0x163)) {
                    p_fade_step(n);
                    if (NB(n, 0x158) == 0xFF) continue;         /* fade ended with a stop */
                }
                if (NB(n, 0x165)) p_dig_check(n);
                else p_sequence(n, si);
                if (NB(n, 0x158) == 0xFF) continue;             /* stopped: the list shifted */
            }
            si++;
        }
        p_dig_service();
        p_send_pending_volumes();
        drv(3, 0, 0);
    }
    UNLOCK();
}

bool player_test_listed(FarPtr n) { return p_find(n) != 0xFF; }

/* Program switch / shutdown: stop calling the service from the audio thread. */
void snd_host_detach(void)
{
    host_set_sound_service(NULL);
    LOCK();
    pl_seg = 0;
    UNLOCK();
}
