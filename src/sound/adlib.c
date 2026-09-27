/* The SCI1 AdLib driver "dude" (chunk SSM:ADL: of SXCODE.OVL / SX.OVL / SX1.OVL, 8070 bytes, 18
 * functions selected by BP) — sound.md §8. Not part of the EXEs: the original runs the driver code
 * loaded from the OVL; the port runs this model on the same memory block.
 *
 * State: the driver is position independent and keeps every variable cs-relative inside its own image
 * (tables, per-channel and per-voice state, the patch bank at 08F2h). The model does the same: all
 * reads and writes go to the driver block in mem[] at the original offsets (16-bit wrap inside the
 * segment), so out-of-range table reads behave like the original. Verified write-for-write against
 * tools/aotpsnd.py (AdlibDriver) with AOTP_TEST=dro.
 *
 * Hardware: opl_write (1b13: out 388h reg, 6 status reads, out 389h val, 35 status reads) =
 * snd_opl_write -> host_opl_write (Nuked-OPL3, OPL2 mode). PORT: the status-read delays are dropped. */
#include "sound/snd_priv.h"

#include "host.h"

void (*snd_opl_write)(u8 reg, u8 val) = host_opl_write;

/* Driver data offsets (sound.md §8.2). */
enum {
    D_BLOCK = 0x0039, D_FNUM = 0x0236, D_VELCURVE = 0x0630, D_VOLCURVE = 0x0670,
    D_REVERB = 0x06B0, D_SOUNDON = 0x06B1, D_MASTER = 0x06B2,
    D_PROG = 0x06B3, D_VOL = 0x06C3, D_PAN = 0x06D3, D_HOLD = 0x06E3, D_OWED = 0x06F3,
    D_BEND = 0x0703, D_LAST = 0x0723,
    D_OWNER = 0x0733, D_NOTE = 0x073E, D_VELO = 0x0749, D_SUST = 0x0754, D_AGE = 0x075F,
    D_INSTR = 0x0775, D_CAR = 0x0780, D_MOD = 0x0789, D_ISCAR = 0x0792, D_REGOFF = 0x07A4,
    D_OPCH = 0x07B6, D_PAIRS = 0x07C8, D_RESET_MOD = 0x07DA, D_RESET_CAR = 0x07E8,
    D_SHADOW = 0x07F6, D_BANK = 0x08F2, D_RESET_WORK = 0x1372,
    D_NOTESEL = 0x1380, D_AMDEPTH = 0x1381, D_VIBDEPTH = 0x1382, D_RHYTHM = 0x1383, D_BDLOW = 0x1384,
    D_WSEL = 0x1385,
    D_CKSL = 0x138A, D_CTL = 0x13A0, D_CTL15 = 0x13B6, D_MKSL = 0x13CC, D_MTL = 0x13E2,
    D_MSCALED = 0x13F8, D_MTL15 = 0x1403,
};

static FarPtr drv_base;              /* the driver block of the current call (set by adl_driver) */

static inline u8 *dp(u16 o) { return mp(drv_base.seg, (u16)(drv_base.off + o)); }
static inline u8  db(u16 o) { return *dp(o); }
static inline void sb(u16 o, u8 v) { *dp(o) = v; }
static inline u16 dw(u16 o) { return (u16)(db(o) | db((u16)(o + 1)) << 8); }
static inline void sw(u16 o, u16 v) { sb(o, (u8)v); sb((u16)(o + 1), (u8)(v >> 8)); }

static void out(u16 reg, u16 val)    /* 1b13 */
{
    snd_opl_write((u8)reg, (u8)val);
}

static void set_volume(int v, u8 al);
static void set_note(int v, u8 note, int keyon);

/* 1958: index = note*4 moved by the channel's pitch bend in quarter-tone steps; FFFF = out of range */
static u16 bend_index(u8 ch, u16 cx)
{
    u16 di = dw((u16)(D_BEND + 2 * ch));
    if (di == 0x2000) return cx < 0x1FC ? cx : 0xFFFF;
    bool up = di > 0x2000;
    u16 d = (u16)(((up ? (u16)(di - 0x2000) : (u16)(0x2000 - di)) / 0xAA) & 0xFF);
    cx = (u16)(up ? cx + d : cx - d);
    return cx < 0x1FC ? cx : 0xFFFF;
}

/* 18ab: frequency + key, then volume */
static void set_note(int v, u8 note, int keyon)
{
    sb((u16)(D_NOTE + v), note);
    u8 ch = db((u16)(D_OWNER + v));
    u16 cx = bend_index(ch, (u16)(note << 2));
    if (cx == 0xFFFF) return;
    u16 fnum = dw((u16)(D_FNUM + 2 * cx));
    out((u16)(0xA0 + v), fnum & 0xFF);
    u8 al = (u8)((keyon << 3 | db((u16)(D_BLOCK + cx))) << 2);
    out((u16)(0xB0 + v), (u8)(al | fnum >> 8));
    u8 a = (u8)(db((u16)(D_VOL + ch)) + 1);
    u8 c = (u8)(db((u16)(D_VELCURVE + db((u16)(D_VELO + v)))) + 1);
    u16 ax = (u16)((a * c) >> 6);
    ax = (u16)(((ax & 0xFF) * (u8)(db(D_MASTER) + 1)) >> 4);
    al = (u8)ax;
    if (al) al--;
    if (db(D_SOUNDON) == 0) al = 0;
    set_volume(v, al);
}

/* 19b2: TL = 63 - vol * (63 - patch TL) / 63; the modulator too for additive instruments */
static void set_volume(int v, u8 al)
{
    al = db((u16)(D_VOLCURVE + al));
    u8 cl = (u8)dw((u16)(D_CTL + 2 * v));
    u8 q = (u8)(((u16)(al * cl)) / 0x3F);
    u8 tl = (u8)(0x3F - q);
    tl |= (u8)(dw((u16)(D_CKSL + 2 * v)) << 6);
    out((u16)(0x40 + db((u16)(D_REGOFF + db((u16)(D_CAR + v))))), tl);
    if (db((u16)(D_MSCALED + v))) {
        cl = (u8)dw((u16)(D_MTL + 2 * v));
        q = (u8)(((u16)(al * cl)) / 0x3F);
        tl = (u8)(0x3F - q);
        tl |= (u8)(dw((u16)(D_MKSL + 2 * v)) << 6);
        out((u16)(0x40 + db((u16)(D_REGOFF + db((u16)(D_MOD + v))))), tl);
    }
}

static void write_bd(void)          /* 1bcf */
{
    out(0xBD, (db(D_AMDEPTH) ? 0x80 : 0) | (db(D_VIBDEPTH) ? 0x40 : 0) | db(D_BDLOW));
}

/* 1c20 + 1c69: operator shadow + registers from 13 operator bytes at driver offset p */
static void write_op(int op, u16 p, u8 wf)
{
    u16 s = (u16)(D_SHADOW + 14 * op);
    for (int i = 0; i < 13; i++) sb((u16)(s + i), db((u16)(p + i)));
    sb((u16)(s + 13), wf & 3);
    u8 off = db((u16)(D_REGOFF + op));
    write_bd();                                                                   /* 1bcf */
    out(0x08, db(D_NOTESEL) ? 0x40 : 0);                                          /* 1cca */
    out((u16)(0x40 + off), (u8)(db(s) << 6 | (db((u16)(s + 8)) & 0x3F)));         /* 1c82 KSL/TL */
    if (db((u16)(D_ISCAR + op)) == 0) {                                           /* 1ce2 */
        u16 c = (u16)((db((u16)(s + 2)) << 1) + (db((u16)(s + 12)) == 0 ? 1 : 0));
        out((u16)(0xC0 + db((u16)(D_OPCH + op))), c & 0x0F);
    }
    out((u16)(0x60 + off), (u8)(db((u16)(s + 3)) << 4 | (db((u16)(s + 6)) & 0x0F)));   /* 1d32 AR/DR */
    out((u16)(0x80 + off), (u8)(db((u16)(s + 4)) << 4 | (db((u16)(s + 7)) & 0x0F)));   /* 1d78 SL/RR */
    u8 c = (u8)((db((u16)(s + 9)) ? 0x80 : 0) | (db((u16)(s + 10)) ? 0x40 : 0) |
                (db((u16)(s + 5)) ? 0x20 : 0) | (db((u16)(s + 11)) ? 0x10 : 0) | (db((u16)(s + 1)) & 0x0F));
    out((u16)(0x20 + off), c);                                                    /* 1dbe */
    if (dw(D_WSEL)) out((u16)(0xE0 + off), db((u16)(s + 13)));                    /* 1e2c */
}

/* 1a4e: 28-byte instrument at driver offset p into voice v */
static void load_patch(int v, u16 p)
{
    sb((u16)(D_MSCALED + v), 1);
    if (db((u16)(p + 0x0C))) {
        sb((u16)(D_MSCALED + v), 0);                /* FM: modulator TL not scaled by the volume */
    } else {
        sw((u16)(D_MKSL + 2 * v), db(p));
        u8 tlc = (u8)(0x3F - db((u16)(p + 8)));
        sw((u16)(D_MTL + 2 * v), tlc);
        sw((u16)(D_MTL15 + 2 * v), (u16)(tlc / 15));
    }
    sw((u16)(D_CKSL + 2 * v), db((u16)(p + 0x0D)));
    u8 tlc = (u8)(0x3F - db((u16)(p + 0x15)));
    sw((u16)(D_CTL + 2 * v), tlc);
    sw((u16)(D_CTL15 + 2 * v), (u16)(tlc / 15));
    write_op(db((u16)(D_PAIRS + 2 * v)), p, db((u16)(p + 0x1A)));
    write_op(db((u16)(D_PAIRS + 2 * v + 1)), (u16)(p + 0x0D), db((u16)(p + 0x1B)));
}

/* 1e6b: chip reset */
static void reset(void)
{
    out(0x01, 0);
    sb(D_RHYTHM, 0);                                /* 1b53 */
    sb(D_BDLOW, 0);
    for (int op = 0; op < 18; op++) {
        u16 src = db((u16)(D_ISCAR + op)) ? D_RESET_CAR : D_RESET_MOD;
        for (int i = 0; i < 13; i++) sb((u16)(D_RESET_WORK + i), db((u16)(src + i)));
        write_op(op, D_RESET_WORK, 0);
    }
    write_bd();
    sb(D_AMDEPTH, 0);
    sb(D_VIBDEPTH, 0);
    sb(D_NOTESEL, 0);
    write_bd();
    out(0x08, db(D_NOTESEL) ? 0x40 : 0);
    for (int v = 0; v < 9; v++) {                   /* 1afb */
        out((u16)(0xA0 + v), 0);
        out((u16)(0xB0 + v), 0);
    }
    sw(D_WSEL, 0x20);                               /* 1b90(1) */
    for (int op = 0; op < 18; op++) out((u16)(0xE0 + db((u16)(D_REGOFF + op))), 0);
    out(0x01, 0x20);
}

static void voice_off(int v)        /* 1865 */
{
    sb((u16)(D_SUST + v), 0);
    set_note(v, db((u16)(D_NOTE + v)), 0);
    sb((u16)(D_NOTE + v), 0xFF);
    sw((u16)(D_AGE + 2 * v), 0);
}

static void voice_on(int v, u8 note, u8 vel)   /* 17f9 */
{
    sw((u16)(D_AGE + 2 * v), 0);
    u8 prog = db((u16)(D_PROG + db((u16)(D_OWNER + v))));
    if (prog != db((u16)(D_INSTR + v)) && db(D_SOUNDON)) {
        sb((u16)(D_INSTR + v), prog);
        load_patch(v, (u16)(D_BANK + prog * 28));
    }
    sb((u16)(D_VELO + v), vel);
    set_note(v, note, 1);
}

/* 15da: a free voice of the channel after the last one used, else steal its oldest (last one wins
 * ties); FF when every voice of the channel was started this tick (the note is dropped, quirk). */
static u8 alloc_voice(u8 ch)
{
    u16 dx = 0;
    u8 cl = 0;
    u8 last = db((u16)(D_LAST + ch));
    u8 bl = last;
    for (;;) {
        bl++;
        if (bl == 9) bl = 0;
        bool stop = bl == last;
        if (db((u16)(D_OWNER + bl)) == ch) {
            if (db((u16)(D_NOTE + bl)) == 0xFF) {
                sb((u16)(D_LAST + ch), bl);
                return bl;
            }
            if (dw((u16)(D_AGE + 2 * bl)) >= dx) {
                dx = dw((u16)(D_AGE + 2 * bl));
                cl = bl;
            }
        }
        if (stop) break;
    }
    if (dx == 0) return 0xFF;
    voice_off(cl);
    sb((u16)(D_LAST + ch), cl);
    return cl;
}

static void update_channel(u8 ch)   /* every sounding voice of ch: frequency + volume again */
{
    for (int v = 0; v < 9; v++)
        if (db((u16)(D_OWNER + v)) == ch && db((u16)(D_NOTE + v)) != 0xFF) set_note(v, db((u16)(D_NOTE + v)), 1);
}

static void assign(u8 ch, u8 cl)    /* 16d4 */
{
    for (int v = 0; v < 9; v++) {
        if (db((u16)(D_OWNER + v)) == 0xFF) {
            sb((u16)(D_OWNER + v), ch);
            cl--;
            if (cl == 0) break;
        }
    }
    sb((u16)(D_OWED + ch), (u8)(db((u16)(D_OWED + ch)) + cl));
}

static void release(u8 ch, u8 cl)   /* 16ff */
{
    u8 ex = db((u16)(D_OWED + ch));
    if (ex >= cl) {
        sb((u16)(D_OWED + ch), (u8)(ex - cl));
        return;
    }
    cl = (u8)(cl - ex);
    sb((u16)(D_OWED + ch), 0);
    for (int v = 0; v < 9; v++) {
        if (db((u16)(D_OWNER + v)) == ch && db((u16)(D_NOTE + v)) == 0xFF) {
            sb((u16)(D_OWNER + v), 0xFF);
            if (--cl == 0) return;
        }
    }
    for (int v = 0; v < 9; v++) {
        if (db((u16)(D_OWNER + v)) == ch) {
            voice_off(v);
            sb((u16)(D_OWNER + v), 0xFF);
            if (--cl == 0) return;
        }
    }
}

static void redistribute(void)      /* 167d */
{
    u8 cl = 0;
    for (int v = 0; v < 9; v++)
        if (db((u16)(D_OWNER + v)) == 0xFF) cl++;
    if (cl == 0) return;
    for (int si = 0; si < 16; si++) {
        u8 o = db((u16)(D_OWED + si));
        if (o == 0) continue;
        if (o < cl) {
            cl = (u8)(cl - o);
            sb((u16)(D_OWED + si), 0);
            assign((u8)si, o);
        } else {
            sb((u16)(D_OWED + si), (u8)(o - cl));
            assign((u8)si, cl);
            return;
        }
    }
}

static void voice_map(u8 ch, u8 n)  /* 1641: controller 4Bh, voices wanted by a channel */
{
    u8 dl = 0;
    for (int v = 0; v < 9; v++)
        if (db((u16)(D_OWNER + v)) == ch) dl++;
    dl = (u8)(dl + db((u16)(D_OWED + ch)));
    if (dl == n) return;
    if (dl < n) {
        assign(ch, (u8)(n - dl));
    } else {
        release(ch, (u8)(dl - n));
        redistribute();
    }
}

static void note_off(u8 ch, u8 note)   /* fn 4, 145a */
{
    for (int v = 0; v < 9; v++) {
        if (db((u16)(D_OWNER + v)) == ch && db((u16)(D_NOTE + v)) == note) {
            if (db((u16)(D_HOLD + ch))) sb((u16)(D_SUST + v), 1);
            else voice_off(v);
            return;
        }
    }
}

static void note_on(u8 ch, u8 note, u8 vel)   /* fn 5, 1490 */
{
    if (vel == 0) { note_off(ch, note); return; }
    if (note < 0x0C || note > 0x6B) return;
    vel >>= 1;
    for (int v = 0; v < 9; v++) {
        if (db((u16)(D_OWNER + v)) == ch && db((u16)(D_NOTE + v)) == note) {
            voice_off(v);
            voice_on(v, note, vel);
            return;
        }
    }
    u8 v = alloc_voice(ch);
    if (v != 0xFF) voice_on(v, note, vel);
}

static void controller(u8 ch, u8 ctl, u8 val)  /* fn 7, 14d8 */
{
    switch (ctl) {
    case 0x07:                                  /* 1762 volume */
        sb((u16)(D_VOL + ch), val >> 1);
        update_channel(ch);
        break;
    case 0x0A:                                  /* 1799 pan: stored, no audible effect on OPL2 */
        sb((u16)(D_PAN + ch), val);
        update_channel(ch);
        break;
    case 0x40:                                  /* 17ca hold pedal */
        sb((u16)(D_HOLD + ch), val);
        if (val == 0)
            for (int v = 0; v < 9; v++)
                if (db((u16)(D_OWNER + v)) == ch && db((u16)(D_SUST + v))) voice_off(v);
        break;
    case 0x4B:                                  /* 1641 voices */
        voice_map(ch, val);
        break;
    case 0x7B:                                  /* all notes off */
        for (int v = 0; v < 9; v++)
            if (db((u16)(D_OWNER + v)) == ch && db((u16)(D_NOTE + v)) != 0xFF) voice_off(v);
        break;
    default:                                    /* 1, 4Eh ...: ignored */
        break;
    }
}

/* fn 17, 1eb1 AskDriver. No caller in the player; ported for completeness. */
static u16 ask_driver(u8 ah, u8 ch, u8 ctl, u8 cl)
{
    ch &= 0x0F;
    switch (ah) {
    case 0xE0: return dw((u16)(D_BEND + 2 * ch));
    case 0xC0: return db((u16)(D_PROG + ch));
    case 0xB0:
        if (ctl == 0x07) return db((u16)(D_VOL + ch));
        if (ctl == 0x0A) return db((u16)(D_PAN + ch));
        if (ctl == 0x40) return db((u16)(D_HOLD + ch));
        if (ctl == 0x4B) {
            if (cl != 0xFF) return db((u16)(D_OWED + ch));
            u16 n = 0;
            for (int v = 0; v < 9; v++)
                if (db((u16)(D_OWNER + v)) == ch) n++;
            return n;
        }
        break;
    }
    return 0xFFFF;
}

static u8 master_volume(u8 cl)      /* fn 12, 1595 */
{
    u8 old = db(D_MASTER);
    if (cl != 0xFF) {
        sb(D_MASTER, cl);
        for (int v = 0; v < 9; v++)
            if (db((u16)(D_NOTE + v)) != 0xFF) set_note(v, db((u16)(D_NOTE + v)), 1);
    }
    return old;
}

/* Entry 143d: call [cs:1419 + 2*BP]. */
void adl_driver(FarPtr drv, u16 bp, DrvRegs *r)
{
    FarPtr saved = drv_base;                    /* (the service may run while an API call is inside) */
    drv_base = drv;
    u8 al = (u8)r->ax, ah = (u8)(r->ax >> 8), cl = (u8)r->cx, ch = (u8)(r->cx >> 8);
    switch (bp) {
    case 0:                                     /* 1f7d PatchReq: patch 3, play id 0, 9 voices */
        r->ax = 0x0103;
        r->cx = 0x0009;
        break;
    case 1: {                                   /* 1f2e Init: ES:AX = patch bank */
        FarPtr p = far_make(r->es, r->ax);
        for (u16 i = 0; i < 0x540; i++) sb((u16)(D_BANK + i), *far_mp(far_add(p, i)));
        if (*far_mp(far_add(p, 0x540)) == 0xAB && *far_mp(far_add(p, 0x541)) == 0xCD)
            for (u16 i = 0; i < 0x540; i++) sb((u16)(D_BANK + 0x540 + i), *far_mp(far_add(p, (u16)(0x542 + i))));
        reset();
        master_volume(15);
        r->ax = 0;
        r->cx = 0x0F00;                         /* CL = 0 first, CH = 15 last device channel */
        break;
    }
    case 2:                                     /* 15d6 Terminate */
        reset();
        break;
    case 3:                                     /* 188f Service: ages of the sounding voices */
        for (int v = 0; v < 9; v++)
            if (db((u16)(D_NOTE + v)) != 0xFF) sw((u16)(D_AGE + 2 * v), (u16)(dw((u16)(D_AGE + 2 * v)) + 1));
        break;
    case 4: note_off(al, ch); break;            /* 145a */
    case 5: note_on(al, ch, cl); break;         /* 1490 */
    case 7: controller(al, ch, cl); break;      /* 14d8 */
    case 8: sb((u16)(D_PROG + al), cl); break;  /* 1522: used at the next note on */
    case 10:                                    /* 1530 PitchBend: CH lsb, CL msb */
        sw((u16)(D_BEND + 2 * al), (u16)(cl << 7 | ch));
        for (int v = 0; v < 9; v++)             /* every voice of the channel, sounding or not (quirk) */
            if (db((u16)(D_OWNER + v)) == al) set_note(v, db((u16)(D_NOTE + v)), 1);
        break;
    case 11: {                                  /* 15c5 Reverb: stored only */
        u8 old = db(D_REVERB);
        if (cl != 0xFF) sb(D_REVERB, cl);
        r->ax = (u16)((r->ax & 0xFF00) | old);
        break;
    }
    case 12:                                    /* 1595 MasterVolume */
        r->ax = (u16)((r->ax & 0xFF00) | master_volume(cl));
        break;
    case 13: {                                  /* 1565 SoundOn */
        u8 old = db(D_SOUNDON);
        if (cl != 0xFF) {
            sb(D_SOUNDON, cl);
            for (int v = 0; v < 9; v++)
                if (db((u16)(D_NOTE + v)) != 0xFF) set_note(v, db((u16)(D_NOTE + v)), 1);
        }
        r->ax = (u16)((r->ax & 0xFF00) | old);
        break;
    }
    case 17:                                    /* 1eb1 AskDriver */
        r->ax = ask_driver(ah, al, ch, cl);
        break;
    default:                                    /* 6, 9, 14-16: ret */
        break;
    }
    drv_base = saved;
}
