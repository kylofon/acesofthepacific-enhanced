/* render3d — macros.c (AP 34f1): the macro table (MBL member groups) and the per-frame member arena.
 * render3d_world.md §5.1, §6.2. */
#include "render3d/r3d_priv.h"
#include "platform/platform.h"

#define MACRO_TAG      far_make(SEG(0x3B01), 0)      /* "macros.c" (3b01:0000) for the fatal messages */
#define MACRO_BUF_SEG  SEG(0x3B05)                   /* static expansion buffer 3B05:0000 (32 members) */

static void fmemset(FarPtr p, u8 c, u16 n) { memset(far_mp(p), c, n); }   /* _fmemset 0000:4b03 */

/* AP 34f1:000b — macro table of n entries, member arena of m 22-byte records */
void macros_alloc(u16 n, u16 m)
{
    u16 di = (u16)(m * 0x16);
    ds_far_wr(W_MACRO_TAB, mem_far_new((u16)(n * 0x12), 0));
    if (far_is_null(ds_far(W_MACRO_TAB))) assert_fail(MACRO_TAG, 0x64, 0);
    DSW(W_MACRO_CAP) = n;
    ds_far_wr(W_TMP_ARENA, mem_far_new((u32)(s32)(s16)di, 0));
    if (far_is_null(ds_far(W_TMP_ARENA))) assert_fail(MACRO_TAG, 0x68, 0);
    DSW(W_ARENA_SIZE1) = (u16)(di + 1);                               /* arena size + 1 */
}

/* AP 34f1:00a8 */
void macros_free(void)
{
    if (!far_is_null(ds_far(W_MACRO_TAB))) { mem_far_delete(ds_far(W_MACRO_TAB)); ds_far_wr(W_MACRO_TAB, (FarPtr){ 0, 0 }); }
    if (!far_is_null(ds_far(W_TMP_ARENA))) { mem_far_delete(ds_far(W_TMP_ARENA)); ds_far_wr(W_TMP_ARENA, (FarPtr){ 0, 0 }); }
}

/* AP 34f1:00e9 — free every entry's members, clear the table */
void macros_clear(void)
{
    FarPtr e = ds_far(W_MACRO_TAB);
    for (s16 i = 0; i < (s16)DSW(W_MACRO_COUNT); i++, e.off = (u16)(e.off + 0x12)) {
        FarPtr m = far_rd(e.seg, (u16)(e.off + 0x0E));
        if (!far_is_null(m)) mem_far_delete(m);
    }
    DSW(W_MACRO_COUNT) = 0;
    fmemset(ds_far(W_MACRO_TAB), 0, (u16)(DSW(W_MACRO_CAP) * 0x12));
}

/* AP 34f1:0144 arena_reset */
void ap_arena_reset_34f1(void) { DSW(W_ARENA_BOTTOM) = 0; DSW(W_ARENA_TOP) = DSW(W_ARENA_SIZE1); }

/* AP 34f1:0155 — copy the group's members to 3B05:0000, rotated by the group heading and moved to its
 * x/y (z not added); returns the member count */
u16 macro_expand(FarPtr g)
{
    u16 type = rd16(g.seg, (u16)(g.off + 2));
    FarPtr mt = ds_far(W_MACRO_TAB);
    FarPtr M = far_add(mt, (u16)((type & 0x3FFF) * 0x12));
    FarPtr dst = far_make(MACRO_BUF_SEG, 0);
    FarPtr src = far_rd(M.seg, (u16)(M.off + 0x0E));
    s32 px = (s32)rd32(g.seg, (u16)(g.off + 4)), py = (s32)rd32(g.seg, (u16)(g.off + 8));
    u16 h = rd16(g.seg, (u16)(g.off + 0x10));
    s16 n = (s16)rd16(M.seg, M.off);
    for (s16 si = 0; n > si; si++) {
        memmove(far_mp(dst), far_mp(src), 0x16);                /* SCOPY@ */
        if (h) {
            lrot_z(far_add(dst, 4), h);
            wr16(dst.seg, (u16)(dst.off + 0x14), h);
        }
        wr32(dst.seg, (u16)(dst.off + 4), rd32(dst.seg, (u16)(dst.off + 4)) + (u32)px);
        wr32(dst.seg, (u16)(dst.off + 8), rd32(dst.seg, (u16)(dst.off + 8)) + (u32)py);
        src = far_add(src, 0x16);
        dst = far_add(dst, 0x16);
        n = (s16)rd16(M.seg, M.off);
    }
    return (u16)n;
}

/* AP 34f1:0259 — length of the group's extent (tiled) or of its macro box (short form) */
u32 macro_distance(FarPtr g)
{
    u16 m = stk_mark(), z = stk_alloc(12), v = stk_alloc(12);
    memcpy(DSP(z), DSP(W_ZERO_VEC), 12);
    u16 type = rd16(g.seg, (u16)(g.off + 2));
    u32 r = 0;
    if ((type & 0xC000) == 0xC000) memcpy(DSP(v), far_mp(far_add(g, 0x1C)), 12);
    else if ((type & 0xC000) == 0x8000) {
        FarPtr M = far_add(ds_far(W_MACRO_TAB), (u16)((type & 0x7FFF) * 0x12));
        memcpy(DSP(v), far_mp(far_add(M, 2)), 12);
    } else { stk_release(m); return 0; }
    r = ldist3(z, v);
    stk_release(m);
    return r;
}

/* AP 34f1:02f5 — no callers */
u32 macro_entry(u16 i)
{
    FarPtr mt = ds_far(W_MACRO_TAB);
    return (u32)mt.seg << 16 | (u16)(mt.off + (i & 0x1FFF) * 0x12);
}

/* AP 34f1:0310 — the AW<n>.MBL macro file (world.md §3) */
void macros_load(u16 name)
{
    u16 fp = my_fopen(name, W_RB_STR);
    if (!fp) assert_fail(MACRO_TAG, 0x1A8, name);
    FarPtr e = ds_far(W_MACRO_TAB);
    u16 mk = stk_mark(), b1 = stk_alloc(2), cnt = stk_alloc(2), nm = stk_alloc(10), n = stk_alloc(2),
        box = stk_alloc(12), rec = stk_alloc(0x16);
    my_fread(b1, 1, 1, fp);
    my_fread(cnt, 2, 1, fp);
    for (;;) {
        u16 c = DSW(cnt); DSW(cnt)--;
        if (!c) break;
        u16 k = DSW(W_MACRO_COUNT); DSW(W_MACRO_COUNT)++;
        if ((s16)k >= (s16)DSW(W_MACRO_CAP)) break;
        DSB(nm + 8) = 0;
        my_fread(nm, 8, 1, fp);
        my_fread(n, 2, 1, fp);
        my_fread(box, 12, 1, fp);
        wr16(e.seg, e.off, DSW(n));
        memcpy(far_mp(far_add(e, 2)), DSP(box), 12);
        FarPtr mem = mem_far_new((u16)(DSW(n) * 0x16), 0);
        far_wr(e.seg, (u16)(e.off + 0x0E), mem);
        if (far_is_null(mem)) assert_fail(MACRO_TAG, 0x1C3, 0);
        for (;;) {
            u16 c2 = DSW(n); DSW(n)--;
            if (!c2) break;
            my_fread(rec, 0x16, 1, fp);
            if (DSW(rec + 2) != 0xFFFF) { memcpy(far_mp(mem), DSP(rec), 0x16); mem = far_add(mem, 0x16); }
            else wr16(e.seg, e.off, (u16)(rd16(e.seg, e.off) - 1));
        }
        e = far_add(e, 0x12);
    }
    stk_release(mk);
    my_fclose(fp);
}

/* AP 34f1:04a3 — a 30-byte A record from the bottom of the arena (zeroed), 0 when full */
u32 arena_alloc30(void)
{
    u16 t = (u16)(DSW(W_ARENA_BOTTOM) + 0x1E);
    if (t > DSW(W_ARENA_TOP)) return 0;
    FarPtr a = ds_far(W_TMP_ARENA);
    FarPtr p = far_add(a, DSW(W_ARENA_BOTTOM));
    DSW(W_ARENA_BOTTOM) = t;
    fmemset(p, 0, 0x1E);
    return (u32)p.seg << 16 | p.off;
}
/* AP 34f1:04f0 — a 40-byte B record from the top (the first one ends one byte past the block) */
u32 arena_alloc40(void)
{
    u16 t = (u16)(DSW(W_ARENA_TOP) - 0x28);
    if (t < DSW(W_ARENA_BOTTOM)) return 0;
    DSW(W_ARENA_TOP) = t;
    FarPtr p = far_add(ds_far(W_TMP_ARENA), t);
    fmemset(p, 0, 0x28);
    return (u32)p.seg << 16 | p.off;
}
/* AP 34f1:053a / 0548 — release the last A / B record */
void arena_free30(void) { DSW(W_ARENA_BOTTOM) = (u16)(DSW(W_ARENA_BOTTOM) - 0x1E); }
void arena_free40(void) { DSW(W_ARENA_TOP) = (u16)(DSW(W_ARENA_TOP) + 0x28); }
