/* The Dynamix sound API (INTRO 157f..172c, PS 4516..46c3, AP 2fca..3326 around the player) and the TTM
 * sound opcodes (INTRO 11d2:000c / PS 3baf:000d) — sound.md §6, §7, §9, §12.1, sound_extra.md §1.
 *
 * Shared library code: the globals go through the layout table (G_snd_*; the rows missing in some
 * programs were added in port/spec/sound_extra2_symbols.csv). Sound records (0x14
 * bytes), nodes (0x17A) and channel lists live in DOS / near-heap memory in mem[] like the original.
 *
 * PORT (sound.md §13, PLAN.md decision 1): every driver number loads and runs the AdLib driver model
 * (snd_port_driver); the driver number variable, the play-id switch of the loader and the driver code
 * table stay as in the original so another driver can drop in later. */
#include "sound/snd_priv.h"

#include "codeptr.h"
#include "host.h"
#include "platform/platform.h"
#include "prog.h"
#include "symbols_ap.h"

/* ---- DGROUP globals of the sound API through the layout (port/spec/sound_extra2_symbols.csv); a global
 * a program does not have (AP: whole, inf_path, dat_path, dat_tail, vol_curve) reads as offset 0, as
 * the former per-program table had it */
static u16 sd_ofs(int g)
{
    if (!prog_layout) host_fatal("sound API called with no program running");
    u16 o = prog_layout->ds[g];
    return o == LAYOUT_ABSENT_DS ? 0 : o;
}
#define SD(f) sd_ofs(G_snd_##f)

static const FarPtr FAR_NULL = { 0, 0 };
static inline bool far_eq(FarPtr a, FarPtr b) { return a.off == b.off && a.seg == b.seg; }
static inline u8 *fp(FarPtr p, u16 o) { return mp(p.seg, (u16)(p.off + o)); }
static inline u8 fb(FarPtr p, u16 o) { return *fp(p, o); }
static inline void fb_set(FarPtr p, u16 o, u8 v) { *fp(p, o) = v; }
static inline u16 fw(FarPtr p, u16 o) { return (u16)(fb(p, o) | fb(p, (u16)(o + 1)) << 8); }
static inline void fw_set(FarPtr p, u16 o, u16 v) { fb_set(p, o, (u8)v); fb_set(p, (u16)(o + 1), (u8)(v >> 8)); }
static inline FarPtr ff(FarPtr p, u16 o) { FarPtr r = { fw(p, o), fw(p, (u16)(o + 2)) }; return r; }
static inline void ff_set(FarPtr p, u16 o, FarPtr v) { fw_set(p, o, v.off); fw_set(p, (u16)(o + 2), v.seg); }

/* record (0x14 bytes, sound.md §3.1) */
enum { R_NEXT = 0x00, R_DATA = 0x04, R_LEN = 0x08, R_ID = 0x0A, R_PRIO = 0x0C, R_NODE = 0x0E, R_FLAGS = 0x12 };

static FarPtr pool_at(int i) { return ds_far((u16)(lds(G_snd_pool) + 4 * i)); }

s16 snd_port_driver(s16 n)
{
    (void)n;
    return 2;                              /* PORT: AdLib whatever the configuration says */
}

/* ============================================================================ memory */

/* INTRO 1724:0000 / PS 46bb:0001 / AP 3326:000d — classes 6/8 near heap, others DOS far memory;
 * 2, 3, 4, 7 zero-filled; failure -> error 1. Quirk: a failed near allocation returns DS:0000. */
FarPtr snd_alloc(u32 size, s16 cls)
{
    FarPtr p;
    if (cls == 6 || cls == 8) {
        u16 o;
        switch (prog_current_id()) {
        case PROG_INTRO: o = intro_lib_malloc((u16)size); break;
        case PROG_PS:    o = lib_realloc((u16)size); break;          /* PS 0000:03c1 (malloc wrapper) */
        default:         o = safe_malloc((u16)size); break;
        }
        p = far_make(cur_dgroup, o);
    } else {
        p = dos_alloc(size, 0, 0);
    }
    if (!far_is_null(p) && (cls == 2 || cls == 3 || cls == 4 || cls == 7))
        huge_memset(p.off, p.seg, 0, (u16)size, (u16)(size >> 16));
    if (far_is_null(p)) DSW(SD(error)) = 1;
    return p;
}

/* INTRO 1634:000d / PS 45cb:000e / AP 326c:000d */
void snd_free(FarPtr p, s16 cls)
{
    if (cls != 6 && cls != 8) { dos_free(p); return; }
    if (prog_current_id() == PROG_INTRO) intro_lib_free(p.off);
    else lib_free(p.off);
}

/* INTRO 1700:000f / PS 4698:0000 / AP 3301:0005: p + 2 + p[1] (offset only) */
FarPtr res_skip_header(FarPtr p)
{
    return far_add(p, (u16)(2 + fb(p, 1)));
}

/* ============================================================================ resource reading */

/* INTRO 172c:000e / PS 46c3:000f / AP 332f:000d: the whole packed chunk at the file position */
FarPtr snd_read_all(u16 f, u32 size, u16 len_ds, s16 cls)
{
    FarPtr buf = FAR_NULL;
    s32 n = 0;
    s16 id = pk_open(-1, f, SD(rd_mode), size);
    if (id >= 0) {
        n = pk_size(id);
        buf = snd_alloc((u32)n, cls);
        if (!far_is_null(buf)) {
            u16 got = pk_read(id, buf, (u16)n);
            if ((u32)got != (u32)n) {
                snd_free(buf, cls);
                buf = FAR_NULL;
            }
        }
        pk_close(id);
    }
    if (len_ds && !far_is_null(buf)) DSL(len_ds) = (u32)n;
    return buf;
}

/* INTRO 15b5:01fa / PS 454c:01fb / AP 2fe4:01f5 */
void chlist_free(FarPtr l)
{
    while (!far_is_null(l)) {
        FarPtr cur = l;
        l = ff(l, 4);
        snd_free(cur, 9);
    }
}

/* one byte from the stream into a DGROUP stack byte */
static bool pk_byte(s16 id, u16 b_ds)
{
    return pk_read(id, ds_ptr(b_ds), 1) == 1;
}

/* INTRO 15b5:023d / PS 454c:023e / AP 2fe4:0238: position the stream after the device byte of the
 * track `dev` (84 header, skip byte 1, walk the tracks' 6-byte channel entries); 0 = not found */
s16 res_find_track(s16 id, u8 dev)
{
    u16 m = stk_mark();
    u16 b = stk_alloc(4);                 /* [bp-3] header, [bp-2] channel byte, [bp-1] device */
    s16 r = 0;
    if (!pk_byte(id, b + 1) || DSB(b + 1) != 0x84) goto out;
    if (!pk_byte(id, b + 1)) goto out;
    if (!pk_byte(id, b + 3)) goto out;
    while (DSB(b + 3) != dev) {
        if (DSB(b + 3) == 0xFF) goto out;
        if (!pk_byte(id, b + 2)) goto out;
        while (DSB(b + 2) != 0xFF) {
            pk_seek(id, 5, 1);
            if (!pk_byte(id, b + 2)) goto out;
        }
        if (!pk_byte(id, b + 3)) goto out;
    }
    r = 1;
out:
    stk_release(m);
    return r;
}

/* INTRO 15b5:0422 / PS 454c:0423 / AP 2fe4:041d: insert by ascending offset (an equal offset goes
 * before); a null list stays null */
FarPtr chlist_insert(FarPtr list, FarPtr item)
{
    if (far_is_null(list)) return list;
    if (fw(list, 0) >= fw(item, 0)) {
        ff_set(item, 4, list);
        return item;
    }
    FarPtr prev = list, cur = list;
    do {
        prev = cur;
        cur = ff(cur, 4);
    } while (!far_is_null(cur) && fw(cur, 0) < fw(item, 0));
    ff_set(item, 4, cur);
    ff_set(prev, 4, item);
    return list;
}

/* INTRO 15b5:033e / PS 454c:033f / AP 2fe4:0339: the track's {u16 off, u16 size} list, by offset.
 * Quirk: when an item cannot be allocated the list is freed and still returned. */
FarPtr chlist_read(s16 id)
{
    u16 m = stk_mark();
    u16 b = stk_alloc(2);
    FarPtr head = FAR_NULL;
    pk_byte(id, b);
    while (DSB(b) != 0xFF) {
        FarPtr it = snd_alloc(8, 9);
        if (far_is_null(it)) break;
        ff_set(it, 4, FAR_NULL);
        pk_seek(id, 1, 1);
        pk_read(id, it, 4);
        pk_byte(id, b);
        head = far_is_null(head) ? it : chlist_insert(head, it);
    }
    if (DSB(b) != 0xFF) chlist_free(head);
    stk_release(m);
    return head;
}

/* INTRO 15b5:04ce / PS 454c:04cf / AP 2fe4:04c9: 84 00 dev, per channel 00 00 newoff size, FF FF,
 * then the channels' data (newoff relative to byte 2) */
s16 res_build(s16 id, FarPtr list, FarPtr dst, u16 hdr, u8 dev)
{
    FarPtr p = dst, q = far_add(dst, hdr);
    fb_set(p, 0, 0x84); p = far_add(p, 1);
    fb_set(p, 0, 0);    p = far_add(p, 1);
    fb_set(p, 0, dev);  p = far_add(p, 1);
    while (!far_is_null(list)) {
        fb_set(p, 0, 0);
        fb_set(p, 1, 0);
        fw_set(p, 2, (u16)(q.off - dst.off - 2));
        fw_set(p, 4, fw(list, 2));
        pk_seek(id, (s32)(u16)(fw(list, 0) + 2), 0);
        if (pk_read(id, q, fw(list, 2)) != fw(list, 2)) return 0;
        q = far_add(q, fw(list, 2));
        list = ff(list, 4);
        p = far_add(p, 6);
    }
    fw_set(p, 0, 0xFFFF);
    return 1;
}

/* INTRO 15b5:0001 / PS 454c:0002 / AP 2fe4:0005: classes 4/7 keep only the current driver's device
 * track (AP: always; INTRO/PS unless the never-set "whole resource" flag); others whole. */
FarPtr snd_res_read(u16 f, u32 size, u16 len_ds, s16 cls)
{
    FarPtr list = FAR_NULL;
    bool whole = SD(whole) && DSW(SD(whole)) != 0;
    if (whole || (cls != 4 && cls != 7)) return snd_read_all(f, size, len_ds, cls);
    u8 dev;
    switch (LW(G_snd_drv_no)) {           /* switch cs:01f2 / the 4..7Eh compares */
    case 0: dev = 0x12; break;
    case 1: case 5: case 6: dev = 0x13; break;
    case 2: case 4: case 7: dev = 0x00; break;
    case 3: dev = 0x0C; break;
    case 0x7E: dev = DSB(SD(play7e)); break;
    default: return FAR_NULL;             /* e.g. 88h (NLD), FFFEh: the load fails */
    }
    s16 id = pk_open(-1, f, SD(pk_mode), size);
    if (id >= 0) {
        if (!res_find_track(id, dev) || far_is_null(list = chlist_read(id))) {
            DSW(SD(error)) = 2;
        } else {
            u32 sum = 0;
            u16 hdr = 5;
            for (FarPtr it = list; !far_is_null(it); it = ff(it, 4)) {
                sum += fw(it, 2);
                hdr = (u16)(hdr + 6);
            }
            if (hdr & 1) hdr++;
            if (hdr < 0x26) hdr = 0x26;     /* room for the player's in-place header rewrite */
            u32 total = sum + hdr;
            FarPtr buf = snd_alloc(total + 1, 4);
            if (!far_is_null(buf) && res_build(id, list, buf, hdr, dev)) {
                chlist_free(list);
                if (len_ds) DSL(len_ds) = total;
                pk_close(id);
                return buf;
            }
        }
        pk_close(id);
    }
    chlist_free(list);
    return FAR_NULL;
}

/* INTRO 1620:000e / PS 45b7:000f / AP 304e:0000: find chunk `tag` (occurrence n) in the volume file
 * `name` (or an already open chunk file) and read it whole (class 1). The name is first tried as an
 * open chunk handle, as the original does. */
FarPtr ovl_load_chunk(u16 name_ds, u16 tag_ds, s16 n)
{
    FarPtr r = FAR_NULL;
    u16 h = name_ds;
    s16 was_open = chunk_is_open(h);
    if (!was_open) h = chunk_open(h);
    if (h) {
        if (chunk_find(h, tag_ds, n) != -1) r = snd_res_read(h, (u32)chunk_size(h), 0, 1);
    }
    if (!was_open) chunk_close(h);
    return r;
}

/* ============================================================================ records */

/* INTRO 159b:0006 / PS 4532:0007 / AP 2fca:0004: iterator (sel > 0 id, 0 all, -1 music, -2 effects,
 * -3 continue); global state (quirk: nested use resets it) */
FarPtr snd_rec_iter(s16 sel)
{
    u16 want = 0, mask = 1;
    u16 ir = SD(iter_rec), is = SD(iter_sel);
    if (sel == -3) {
        FarPtr cur = ds_far(ir);
        if (!far_is_null(cur)) ds_far_wr(ir, ff(cur, R_NEXT));
    } else {
        DSW(is) = (u16)sel;
        ds_far_wr(ir, ds_far(lds(G_snd_rec_head)));
    }
    s16 s = (s16)DSW(is);
    if (s == -2) {
        want = 1;
    } else if (s != -1) {
        if (s != 0) {
            FarPtr cur = ds_far(ir);
            if (far_is_null(cur) || sel == -3) {
                ds_far_wr(ir, FAR_NULL);
            } else {
                while (!far_is_null(cur) && (s16)fw(cur, R_ID) != sel) cur = ff(cur, R_NEXT);
                ds_far_wr(ir, cur);
            }
            return ds_far(ir);
        }
        mask = 0;
        want = 1;
    }
    for (FarPtr cur = ds_far(ir); !far_is_null(cur); cur = ff(cur, R_NEXT)) {
        ds_far_wr(ir, cur);
        if ((fw(cur, R_FLAGS) & mask) != want) return cur;
    }
    ds_far_wr(ir, FAR_NULL);
    return FAR_NULL;
}

/* INTRO 157f:0009 / PS 4516:000a: one DAT: chunk (u16 id, u8 prio, u8 flags, packed resource) */
s16 snd_rec_load(u16 f)
{
    u16 m = stk_mark();
    u16 v = stk_alloc(2);                  /* local_10 */
    u16 len = stk_alloc(4);                /* local_a */
    s16 r = 0;
    my_fread(v, 2, 1, f);
    if (!far_is_null(snd_rec_iter((s16)DSW(v)))) { r = 1; goto out; }
    FarPtr rec = snd_alloc(0x14, 3);
    if (far_is_null(rec)) goto out;
    fw_set(rec, R_ID, DSW(v));
    my_fread(v, 1, 1, f);
    fw_set(rec, R_PRIO, DSW(v) & 0xFF);
    my_fread(v, 1, 1, f);
    fw_set(rec, R_FLAGS, DSB(v));
    s16 cls = (fw(rec, R_FLAGS) & 1) ? 4 : 7;
    u32 size = (u32)chunk_size(f) - 4;
    ff_set(rec, R_DATA, FAR_NULL);
    FarPtr data = snd_res_read(f, size, len, cls);
    ff_set(rec, R_DATA, data);
    if (!far_is_null(data)) {
        ff_set(rec, R_NEXT, ds_far(lds(G_snd_rec_head)));
        fw_set(rec, R_LEN, DSW(len));
        ds_far_wr(lds(G_snd_rec_head), rec);
        r = 1;
        goto out;
    }
    snd_free(rec, 3);
out:
    stk_release(m);
    return r;
}

/* INTRO 1659:000b / PS 45f0:000c: id > 0 one sound (INF: order), 0 every DAT: chunk */
s16 snd_load_resource(u16 file_ds, s16 id)
{
    u16 m = stk_mark();
    u16 path = stk_alloc(10), nb = stk_alloc(2), x = stk_alloc(2);
    s16 found = 0, opened = 0;
    for (int i = 0; i < 9; i++) DSB(path + i) = DSB(SD(inf_path) + i);      /* "SND:INF:" */
    if (id > 0 && !far_is_null(snd_rec_iter(id))) { stk_release(m); return 1; }
    u16 h = file_ds;
    if (!chunk_is_open(file_ds)) { opened = 1; h = chunk_open(file_ds); }
    if (id < 1) {
        if (id == 0) {
            for (int i = 0; i < 5; i++) DSB(path + 4 + i) = DSB(SD(dat_tail) + i);   /* "SND:DAT:" */
            while (chunk_find(h, path, 0) != -1) snd_rec_load(h);
            found = 1;
        }
    } else if (chunk_find(h, path, 1) != -1) {
        my_fseek(h, 2, 1);                                   /* version */
        my_fread(nb, 2, 1, h);
        u16 k = 0;
        do {
            my_fread(x, 2, 1, h);
            k++;
            if (DSW(nb) <= k) break;
        } while ((s16)DSW(x) != id);
        if ((s16)DSW(x) == id) {
            for (int i = 0; i < 5; i++) DSB(path + 4 + i) = DSB(SD(dat_path) + i);
            if (chunk_find(h, path, (s16)k) != -1 && snd_rec_load(h)) found = 1;
        }
    }
    if (opened) chunk_close(h);
    stk_release(m);
    return found;
}

/* ============================================================================ nodes and the pool */

/* INTRO 15ac:000c / PS 4543:000d / AP 2fdc:0000 */
FarPtr snd_node_new(FarPtr res)
{
    FarPtr n = snd_alloc(0x17A, 2);
    if (far_is_null(n)) return FAR_NULL;
    ff_set(n, 0x166, res);
    ff_set(n, 0x16A, res_skip_header(res));
    ff_set(n, 0x008, far_add(n, 0x16A));
    fb_set(n, 0x15E, 0x7F);
    ff_set(n, 0x172, FAR_NULL);
    return n;
}

/* INTRO 15a9:0005 / PS 4540:0006 / AP 2fd8:0003. Quirk: the pointers are not cleared. */
s16 snd_pool_free(void)
{
    if (far_is_null(pool_at(0))) return 0;
    for (int i = 0; i < 7; i++)
        if (!far_is_null(pool_at(i))) snd_free(pool_at(i), 2);
    return 1;
}

/* INTRO 1610:0005 / PS 45a7:0006 / AP 3040:000c: 7 effect nodes */
s16 snd_pool_alloc(void)
{
    if (!far_is_null(pool_at(0))) return 0;
    for (int i = 0; i < 7; i++) {
        FarPtr n = snd_alloc(0x17A, 2);
        ds_far_wr((u16)(lds(G_snd_pool) + 4 * i), n);
        if (far_is_null(n)) { snd_pool_free(); return 0; }
        fb_set(n, 0x158, 0xFF);
        ff_set(n, 0x008, far_add(n, 0x16A));
    }
    return 1;
}

/* INTRO 1617:000b / PS 45ae:000c / AP 3048:0006: the pool node playing this data */
FarPtr snd_pool_find(FarPtr res)
{
    for (int i = 0; i < 7; i++) {
        FarPtr n = pool_at(i);
        if (far_eq(ff(n, 0x166), res) && fb(n, 0x158) != 0xFF) return n;
    }
    return FAR_NULL;
}

/* INTRO 170c:000f / PS 46a4:0000 / AP 330d:0001: the first pool node with this data (playing or not) */
void snd_pool_stop_data(FarPtr res)
{
    for (int i = 0; i < 7; i++) {
        FarPtr n = pool_at(i);
        if (far_eq(ff(n, 0x166), res)) {
            snd_node_stop(n);
            fb_set(n, 0x158, 0xFF);
            return;
        }
    }
}

/* INTRO 168b:0002 / PS 4622:0003 / AP 328c:000e */
void snd_pool_stop_all(void)
{
    for (int i = 0; i < 7; i++) {
        FarPtr n = pool_at(i);
        if (fb(n, 0x158) != 0xFF) {
            snd_node_stop(n);
            fb_set(n, 0x158, 0xFF);
        }
    }
}

/* INTRO 1715:000e / PS 46ac:000f / AP 3318:0005: an effect on the first free pool node, play(node, 0) */
FarPtr snd_pool_play(FarPtr res, s16 vol, u8 loop)
{
    if (far_is_null(res)) return FAR_NULL;
    for (int i = 0; i < 7; i++) {
        FarPtr n = pool_at(i);
        if (fb(n, 0x158) != 0xFF) continue;
        ff_set(n, 0x166, res);
        ff_set(n, 0x16A, res_skip_header(res));
        u16 inf = LW(G_snd_sng_inf);
        if (inf == 0) {
            fb_set(n, 0x15D, loop);
            fb_set(n, 0x15C, 1);
            fb_set(n, 0x15E, (u8)vol);
        } else {                           /* AP: vol is the SNG index there */
            fb_set(n, 0x15D, DSB((u16)(inf + vol * 2)));
            fb_set(n, 0x15C, DSB((u16)(inf + vol * 2 + 1)));
            fb_set(n, 0x15E, 0x7F);
        }
        snd_node_play(n, 0);
        return n;
    }
    return FAR_NULL;
}

/* INTRO 1705:0008 / PS 469c:0009 / AP 3305:000c: follow +172h k times */
FarPtr node_nth(FarPtr n, s16 k)
{
    while (!far_is_null(n) && k != 0) {
        n = ff(n, 0x172);
        k--;
    }
    return n;
}

/* INTRO 1702:000a / PS 4699:000b / AP 3303:0000 */
void node_stop_nth(FarPtr n, s16 k)
{
    FarPtr m = node_nth(n, k);
    if (!far_is_null(m)) snd_node_stop(m);
}

/* INTRO 1708:0008 / PS 469f:0009 / AP 3308:000c: play(node, 1) = every channel locked */
FarPtr node_play_nth(FarPtr n, s16 k, u8 vol)
{
    FarPtr m = node_nth(n, k);
    if (far_is_null(m)) return FAR_NULL;
    fb_set(m, 0x15E, vol);
    snd_node_play(m, 1);
    return m;
}

/* AP 303f:0000 */
void snd_node_stop_far(FarPtr n)
{
    snd_node_stop(n);
    fb_set(n, 0x158, 0xFF);
}

/* INTRO 1629:007d / PS 45c0:007e / AP 3056:007a: music -> its node, effect -> its pool node */
FarPtr snd_rec_node(FarPtr rec)
{
    if (fw(rec, R_FLAGS) & 1) return ff(rec, R_NODE);
    return snd_pool_find(ff(rec, R_DATA));
}

/* INTRO 1629:0009 / PS 45c0:000a: the node(s) of a selector (-3 continues). Quirk: for an id that is
 * not loaded the null record is dereferenced like the original (reads low memory). */
FarPtr snd_id_node(s16 sel)
{
    FarPtr rec = snd_rec_iter(sel);
    if (sel > 0) return snd_rec_node(rec);
    while (!far_is_null(rec)) {
        FarPtr n = snd_rec_node(rec);
        if (!far_is_null(n)) return n;
        rec = snd_rec_iter(-3);
    }
    return FAR_NULL;
}

/* ============================================================================ play / stop / classes */

static void stop_music_node(FarPtr rec)
{
    node_stop_nth(ff(rec, R_NODE), 0);
    while (fb(ff(rec, R_NODE), 0x158) != 0xFF) host_pump();     /* immediate (the stop runs under cli) */
    snd_free(ff(rec, R_NODE), 2);
    ff_set(rec, R_NODE, FAR_NULL);
}

/* INTRO 16e6:000f / PS 467e:0000 / AP 32e7:0005 */
s16 snd_stop(s16 sel)
{
    if (sel != -2) {
        if (sel != -1 && sel != 0) {
            FarPtr rec = snd_rec_iter(sel);
            if (far_is_null(rec)) return 0;
            fw_set(rec, R_FLAGS, fw(rec, R_FLAGS) & 0xFFEF);
            if (!(fw(rec, R_FLAGS) & 1)) snd_pool_stop_data(ff(rec, R_DATA));
            else if (!far_is_null(ff(rec, R_NODE))) stop_music_node(rec);
            return 1;
        }
        FarPtr rec = snd_rec_iter(-1);
        while (!far_is_null(rec)) {
            fw_set(rec, R_FLAGS, fw(rec, R_FLAGS) & 0xFFEF);
            if (far_is_null(ff(rec, R_NODE))) rec = snd_rec_iter(-3);
            else { stop_music_node(rec); rec = FAR_NULL; }
        }
        if (sel == -1) return 1;
    }
    for (FarPtr rec = snd_rec_iter(-2); !far_is_null(rec); rec = snd_rec_iter(-3))
        fw_set(rec, R_FLAGS, fw(rec, R_FLAGS) & 0xFFEF);
    snd_pool_stop_all();
    return 1;
}

/* INTRO 16b6:0001 / PS 464d:0002 / AP 3299:0003 */
s16 snd_play(s16 id)
{
    FarPtr rec = ds_far(lds(G_snd_rec_head));
    while (!far_is_null(rec) && (s16)fw(rec, R_ID) != id) rec = ff(rec, R_NEXT);
    if (far_is_null(rec)) return 0;
    u16 fl = fw(rec, R_FLAGS);
    if ((fl & 0x10) || far_is_null(ff(rec, R_DATA)) || !far_is_null(ff(rec, R_NODE))) return 1;
    s16 dis = (s16)DSW(SD(disabled));
    if (!(fl & 1)) {                                   /* effect */
        if (!far_is_null(snd_pool_find(ff(rec, R_DATA)))) return 1;
        if (dis != 0 && dis != -2) {
            snd_pool_play(ff(rec, R_DATA), 0x7F, (fl & 2) ? 1 : 0);
            return 1;
        }
        if (fl & 2) fw_set(rec, R_FLAGS, fl | 0x10);
        return 1;
    }
    for (FarPtr r = ds_far(lds(G_snd_rec_head)); !far_is_null(r); r = ff(r, R_NEXT))   /* one music */
        if ((fw(r, R_FLAGS) & 1) && !far_is_null(ff(r, R_NODE)) && (s16)fw(r, R_ID) != id) snd_stop((s16)fw(r, R_ID));
    if (dis != 0 && dis != -1) {
        FarPtr n = snd_node_new(ff(rec, R_DATA));
        ff_set(rec, R_NODE, n);
        if (far_is_null(n)) return 0;
        fb_set(n, 0x15D, (fw(rec, R_FLAGS) & 2) ? 1 : 0);
        fb_set(n, 0x15C, (u8)fw(rec, R_PRIO));
        return far_is_null(node_play_nth(n, 0, 0x7F)) ? 0 : 1;
    }
    fw_set(rec, R_FLAGS, fl | 0x10);
    return 1;
}

/* INTRO 164d:0006 / PS 45e4:0007 / AP 3280:0009: 0 all, -1 music, -2 effects */
s16 snd_disable(s16 cls)
{
    if (cls > 0) return 0;
    u16 d = SD(disabled);
    s16 old = (s16)DSW(d);
    if (cls != -1) snd_stop(-2);
    if ((s16)DSW(d) == -4 || cls == 0) DSW(d) = (u16)cls;
    else if (cls != (s16)DSW(d)) DSW(d) = 0;
    if (old != (s16)DSW(d)) {
        for (FarPtr rec = snd_rec_iter(cls); !far_is_null(rec); rec = snd_rec_iter(-3)) {
            if (!far_is_null(snd_rec_node(rec))) {
                snd_stop((s16)fw(rec, R_ID));
                if (fw(rec, R_FLAGS) & 3) fw_set(rec, R_FLAGS, fw(rec, R_FLAGS) | 0x10);   /* resume later */
            }
        }
    }
    return 1;
}

/* INTRO 16de:0002 / PS 4675:0003 / AP 32de:0008 */
s16 snd_enable(s16 cls)
{
    if (cls > 0) return 0;
    u16 d = SD(disabled);
    if (cls == 0 || cls == (s16)DSW(d)) DSW(d) = (u16)-4;
    else if ((s16)DSW(d) == 0) DSW(d) = (u16)(cls == -1 ? -2 : -1);
    for (FarPtr rec = snd_rec_iter(cls); !far_is_null(rec); rec = snd_rec_iter(-3)) {
        if (fw(rec, R_FLAGS) & 0x10) {
            fw_set(rec, R_FLAGS, fw(rec, R_FLAGS) & 0xFFEF);
            snd_play((s16)fw(rec, R_ID));
        }
    }
    return 1;
}

/* INTRO 1672:000b / PS 4609:000c / AP 3056:000b */
s16 snd_unload(s16 sel)
{
    u16 m = stk_mark();
    u16 fake = stk_alloc(4);               /* the "previous record" of the list head (stack local) */
    FarPtr prev = ds_ptr(fake);
    s16 r = 0;
    if (sel == 0 || sel == -2) snd_pool_stop_all();
    FarPtr rec = ds_far(lds(G_snd_rec_head));
    while (!far_is_null(rec)) {
        u16 fl = fw(rec, R_FLAGS);
        s16 rid = (s16)fw(rec, R_ID);
        if (sel == 0 || rid == sel || (sel == -1 && (fl & 1)) || (sel == -2 && !(fl & 1))) {
            snd_stop(rid);
            if (far_eq(rec, ds_far(lds(G_snd_rec_head)))) ds_far_wr(lds(G_snd_rec_head), ff(rec, R_NEXT));
            ff_set(prev, R_NEXT, ff(rec, R_NEXT));
            snd_free(ff(rec, R_DATA), (fw(rec, R_FLAGS) & 1) ? 4 : 7);
            snd_free(rec, 3);
            r = 1;
            if (sel > 0) break;
        } else {
            prev = rec;
        }
        rec = ff(prev, R_NEXT);
    }
    stk_release(m);
    return r;
}

/* ============================================================================ per-sound wrappers */

/* INTRO 168d:000f / PS 4625:0000 */
s16 snd_pause(s16 sel)
{
    FarPtr n = snd_id_node(sel);
    if (far_is_null(n)) return 0;
    for (; !far_is_null(n); n = snd_id_node(-3)) snd_node_pause(n, 1);
    return 1;
}

/* INTRO 1693:0005 / PS 462a:0006 */
s16 snd_resume(s16 sel)
{
    FarPtr n = snd_id_node(sel);
    if (far_is_null(n)) return 0;
    for (; !far_is_null(n); n = snd_id_node(-3)) snd_node_pause(n, 0);
    return 1;
}

/* INTRO 1698:000a / PS 462f:000b: 0-63 through the volume curve, not queued */
s16 snd_volume(s16 sel, s16 v)
{
    if (v > 0x3F) v = 0x3F;
    if (v < 0) v = 0;
    FarPtr n = snd_id_node(sel);
    if (far_is_null(n)) return 0;
    for (; !far_is_null(n); n = snd_id_node(-3)) snd_node_volume(n, DSB((u16)(SD(vol_curve) + v)));
    return 1;
}

/* INTRO 16a0:0003 / PS 4637:0004 (first node only) */
s16 snd_controller(s16 sel, s16 ctl, s16 val, s16 chan)
{
    FarPtr n = snd_id_node(sel);
    if (far_is_null(n)) return 0;
    snd_node_controller(n, (u16)ctl, (u16)val, (u16)chan);
    return 1;
}

/* INTRO 16a4:0002 / PS 463b:0003: step = |(target - vol) / ticks| (at least 1), one step every 2 ticks */
s16 snd_fade(s16 sel, s16 target, s16 ticks)
{
    FarPtr n = snd_id_node(sel);
    if (far_is_null(n)) return 0;
    s16 step = idiv16_retry((s16)(target - (s8)fb(n, 0x15E)), ticks, NULL);
    if (step == 0) step = 1;
    else if (step < 0) step = (s16)-step;
    for (; !far_is_null(n); n = snd_id_node(-3)) snd_node_fade(n, (u16)target, 1, (u16)step);
    return 1;
}

/* INTRO 1637:000a / PS 45ce:000b (first node only) */
s16 snd_priority(s16 sel, s16 p)
{
    FarPtr n = snd_id_node(sel);
    if (far_is_null(n)) return 0;
    snd_node_set_priority(n, (u16)p);
    return 1;
}

/* INTRO 161d:0005 / PS 45b4:0006 (first node only) */
s16 snd_hold(s16 sel, s16 v)
{
    FarPtr n = snd_id_node(sel);
    if (far_is_null(n)) return 0;
    snd_node_hold(n, (u16)v);
    return 1;
}

/* INTRO 1712:0002 / PS 46a9:0003 (first node only) */
s16 snd_pitch_bend(s16 sel, s16 value, s16 chan)
{
    FarPtr n = snd_id_node(sel);
    if (far_is_null(n)) return 0;
    snd_node_pitch_bend(n, (u16)value, (u16)chan);
    return 1;
}

/* INTRO 1633:000a / PS 45ca:000b */
s16 snd_master_volume(s16 v)
{
    snd_player_master_volume((u16)v);
    return 1;
}

/* INTRO 163b:0003 / PS 45d2:0004 */
s16 snd_reverb(s16 v)
{
    snd_default_reverb((u16)v);
    return 1;
}

/* ============================================================================ driver, init, shutdown */

/* INTRO 163c:0006 / PS 45d3:0007 / AP 326f:000a: SSM:%03d: from the OVL, player_init(bank).
 * Quirk: after a failed init the freed block stays in patch_block. */
s16 snd_load_patch(u16 ovl_ds, u16 patch_no_ds, s16 noload)
{
    s16 p = (s16)DSW(patch_no_ds);
    if (noload == -1 || p == 0xFF) return player_init(FAR_NULL) != 0xFFFF;
    u16 tag = SD(patch_tag), pb = SD(patch_block);
    DSB(tag + 4) = (u8)(p / 100 + '0');
    DSB(tag + 5) = (u8)(p / 10 % 10 + '0');
    DSB(tag + 6) = (u8)(p % 10 + '0');
    if (!far_is_null(ds_far(pb))) snd_free(ds_far(pb), 1);
    ds_far_wr(pb, ovl_load_chunk(ovl_ds, tag, noload));
    if (far_is_null(ds_far(pb))) return 0;
    if (player_init(res_skip_header(ds_far(pb))) != 0xFFFF) {
        snd_free(ds_far(pb), 1);
        ds_far_wr(pb, FAR_NULL);
        return 1;
    }
    snd_free(ds_far(pb), 1);
    return 0;
}

/* INTRO 16ac:000b / PS 4643:000c / AP 3290:0000: SSM:xxx: from the OVL, attach, patch bank */
s16 snd_load_driver(s16 n, u16 ovl_ds)
{
    n = snd_port_driver(n);                /* PORT: AdLib for every configuration (sound.md §13) */
    u16 tag = SD(drv_tag);
    rtl_strcpy((u16)(tag + 4), DSW((u16)(lds(G_snd_drv_code) + 2 * (n & 0x7F))));
    FarPtr blk = ovl_load_chunk(ovl_ds, tag, 0);
    ds_far_wr(lds(G_snd_drv_block), blk);
    if (!far_is_null(blk)) {
        LW(G_snd_patch_no) = player_attach(blk) & 0xFF;
        LB(G_snd_active) = (u8)snd_load_patch(ovl_ds, lds(G_snd_patch_no), (n & 0xFF80) ? -1 : 0);
    }
    if (n == 0x88) n = 3;
    if (!LB(G_snd_active)) n = (s16)0xFFFE;
    LW(G_snd_drv_no) = (u16)n;
    return (s8)LB(G_snd_active);
}

/* PORT: the service runs on the audio thread; the timer slot keeps its far pointer (so slots and
 * handles match) but calling it from the timer ISR does nothing. */
static void player_service_timer_slot(void) {}

/* INTRO 16d5:008d / PS 466c:008e (AP 32d5:0093): timer client of snd_wait_5 */
void snd_wait_tick(void)
{
    LW(G_snd_wait_count) = (u16)(LW(G_snd_wait_count) - 1);
}

void snd_register_service(void)
{
    codeptr_register_shared(F_player_service, player_service_timer_slot);
    host_set_sound_service(player_service);
}

/* INTRO 1593:0004 / PS 452a:0005 */
s16 snd_init(s16 drv, u16 ovl_ds)
{
    if (LB(G_snd_active)) return 1;
    bool notimer = drv == -1;
    if (notimer) drv = 4;
    if (!snd_load_driver(drv, ovl_ds)) return 0;
    if (!notimer && !LB(G_timer_installed)) {
        timer_install(13);
        DSW(SD(timer_owned)) = 1;
    }
    if (!notimer) {
        snd_register_service();
        DSW(SD(timer_handle)) = (u16)timer_add(codeptr_far_shared(F_player_service), 4);
        if (DSW(SD(timer_handle)) == 0) return 0;
    }
    snd_pool_alloc();
    return 1;
}

/* INTRO 16d5:0054 / PS 466c:0055 / AP 32d5:005a: wait for 5 more service periods */
void snd_wait_5(void)
{
    FarPtr tick;
    if (prog_current_id() == PROG_AP) {
        codeptr_register(PROG_AP, 0x32D50093u, snd_wait_tick);
        tick = codeptr_far(0x32D50093u);
    } else {
        codeptr_register_shared(F_snd_wait_tick, snd_wait_tick);
        tick = codeptr_far_shared(F_snd_wait_tick);
    }
    LW(G_snd_wait_count) = 5;
    int h = timer_add(tick, 4);
    while ((s16)LW(G_snd_wait_count) > 0) host_pump();
    timer_remove(h);
}

/* INTRO 16d5:0001 / PS 466c:0002 / AP 32d5:0007 */
void snd_driver_close(void)
{
    if (LB(G_snd_active)) player_terminate();
    if (DSW(SD(timer_handle)) == 0) {
        player_service();
        player_service();
    } else {
        snd_wait_5();
    }
    LB(G_snd_active) = 0;
    if (!far_is_null(ds_far(lds(G_snd_drv_block)))) snd_free(ds_far(lds(G_snd_drv_block)), 1);
    ds_far_wr(lds(G_snd_drv_block), FAR_NULL);
}

/* INTRO 1686:0004 / PS 461d:0005 */
void snd_shutdown(void)
{
    if (!LB(G_snd_active)) return;
    snd_unload(0);
    if (DSW(SD(timer_handle))) {
        timer_remove((s16)DSW(SD(timer_handle)));
        DSW(SD(timer_handle)) = 0;
        host_set_sound_service(NULL);      /* PORT: the audio thread stops with the timer client */
    }
    if (DSW(SD(timer_owned))) {
        timer_restore();
        DSW(SD(timer_owned)) = 0;
    }
    snd_pool_free();
    snd_driver_close();
    LW(G_snd_drv_no) = 0xFFFE;
}

/* AP 0000:02f1: out 0C4h, 0 (IBM PS/1 audio off). PORT: no PS/1 audio hardware; nothing to do. */
void ps1_audio_off(void) {}

/* ============================================================================ AP SNG bank */

/* AP 32b8:0003: every SSM:SNG: chunk (device-filtered, class 7) -> near table of far pointers, 0
 * terminated; SSM:INF: (loop, priority per sound) -> DS:26A3. */
u16 sng_load(u16 name_ds)
{
    FarPtr tab[204];                        /* the 0x330-byte stack table */
    int n = 0;
    s16 opened = 0, ok = 0;
    FarPtr out = FAR_NULL;
    u16 h = name_ds;
    for (int i = 0; i < 204; i++) tab[i] = FAR_NULL;
    if (!chunk_is_open(h)) { opened = 1; h = chunk_open(h); }
    if (chunk_find(h, 0x267A, 0) != -1) {                       /* "SSM:" */
        while (chunk_find(h, 0x267F, 0) != -1) {                /* "SSM:SNG:" */
            if (n >= 204) host_fatal("sng_load: more than 204 SNG chunks");
            tab[n++] = snd_res_read(h, (u32)chunk_size(h), 0, 7);
        }
        out = snd_alloc((u32)(n + 1) * 4, 6);
        if (!far_is_null(out)) {
            int i;
            for (i = 0; i < n; i++) far_wr(out.seg, (u16)(out.off + 4 * i), tab[i]);
            far_wr(out.seg, (u16)(out.off + 4 * i), FAR_NULL);
            if (chunk_find(h, 0x2688, 0) == -1) {               /* "SSM:INF:" */
                ok = 1;
            } else {
                DSW(AP_DS_snd_sng_inf) = snd_alloc((u32)(n + 1) * 4, 8).off;
                if (DSW(AP_DS_snd_sng_inf)) {
                    my_fread(DSW(AP_DS_snd_sng_inf), (s16)n, 2, h);
                    ok = 1;
                }
            }
        }
    }
    if (opened) chunk_close(h);
    if (!ok) {
        /* PORT: the original also frees the (uninitialised) slot n; the port skips null slots */
        for (int i = n; i >= 0; i--)
            if (!far_is_null(tab[i])) snd_free(tab[i], 4);
        if (out.off) snd_free(out, 6);
        out = FAR_NULL;
    }
    return out.off;
}

/* AP 3312:000a */
void sng_free(u16 table_ds)
{
    for (u16 p = table_ds; !far_is_null(ds_far(p)); p = (u16)(p + 4)) snd_free(ds_far(p), 7);
    snd_free(far_make(cur_dgroup, table_ds), 6);
    if (DSW(AP_DS_snd_sng_inf)) snd_free(far_make(cur_dgroup, DSW(AP_DS_snd_sng_inf)), 8);
}

/* ============================================================================ TTM sound opcodes */

/* INTRO 11d2:000c / PS 3baf:000d: class C000 opcodes of the TTM interpreter (sound.md §9); the opcode
 * is in ttm_op, a..d the operands, str_ds the string operand. Unknown -> -1. */
s16 ttm_sound_op(u16 a, u16 b, u16 c, u16 d, u16 str_ds)
{
    s16 sa = (s16)a, sb = (s16)b, sc = (s16)c, sd = (s16)d;
    switch (LW(G_ttm_op)) {
    case 0xC01F: return (s16)rtl_strcpy(lds(G_ttm_snd_name), str_ds);
    case 0xC02F: return (s16)rtl_strcpy(lds(G_ttm_snd_bank), str_ds);
    case 0xC031: return snd_load_resource(lds(G_ttm_snd_bank), sa);
    case 0xC041: return snd_unload(sa);
    case 0xC051: return snd_play(sa);
    case 0xC061: return snd_stop(sa);
    case 0xC071: return snd_pause(sa);
    case 0xC081: return snd_resume(sa);
    case 0xC091: return snd_disable(sa);
    case 0xC0A1: return snd_enable(sa);
    case 0xC0B2: return snd_priority(sa, sb);
    case 0xC0C2: return snd_hold(sa, sb);
    case 0xC0D3: return snd_pitch_bend(sa, sb, sc);
    case 0xC0E3: return snd_fade(sa, sb, sc);
    case 0xC0F4: return snd_controller(sa, sb, sc, sd);
    case 0xC102: return snd_volume(sa, sb);
    case 0xCF01: return snd_reverb(sa);
    case 0xCF11: return snd_master_volume(sa);
    }
    return -1;
}
