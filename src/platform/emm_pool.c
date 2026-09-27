/* AP EMS bitmaps and emm_pool.c (platform.md §5.4): 1d32:2050..25a5. The EMS model is ems.c
 * (copy-in/copy-out page frame); everything is kept as the original, including the bump-allocator
 * quirks of the pools. */
#include "platform/rtl_priv.h"

#include "video/video.h"

#define POOLS      0x615C   /* 4 x 8Eh */
#define POOL_SIZE  0x8E
#define CUR_POOL   0x207E   /* s16, -1 = none */
#define CUR_FRAME  0x6158   /* far */

#define P_handle  0x00
#define P_cap     0x02
#define P_used    0x06
#define P_mask    0x0A
#define P_off(k)  (0x0C + 2 * (k))
#define P_size(k) (0x4C + 2 * (k))

static u16 pool(s16 p) { return (u16)(POOLS + POOL_SIZE * p); }
#define PW(p, f) DSW((u16)(pool(p) + (f)))
#define PL(p, f) DSL((u16)(pool(p) + (f)))

/* the video driver's entry 13 (bitmap byte size) through the jump table (loaders.c) */
u32 drv_call_bm_size(u16 bmp_ds, u16 out_ds);

/* AP 1d32:2050 bmp_to_ems(bmp): move the pixel block into one EMS handle; frames keep handle+offset */
u16 bmp_to_ems(u16 bmp)
{
    u16 m = stk_mark();
    u16 tmp = stk_alloc(2);
    u32 size = drv_call_bm_size(bmp, tmp);
    stk_release(m);
    if (!LB(G_ems_present)) return bmp;
    u16 h = lib_ems_alloc(size);
    if (!h) return bmp;
    FarPtr f = lib_ems_map(h, 0);
    u16 fr0 = DSW(bmp);
    FarPtr old = far_make(DSW(fr0), DSW((u16)(fr0 + 2)));
    huge_memcpy_fp(f, old, (u16)size);
    s32 base = (s32)(s16)DSW(fr0) << 4 | DSW((u16)(fr0 + 2));
    for (s16 i = (s16)(ap_bmp_count(bmp) - 1); i >= 0; i--) {
        u16 fr = DSW((u16)(bmp + 2 * i));
        s32 lin = (s32)(s16)DSW(fr) << 4 | DSW((u16)(fr + 2));
        DSW(fr) = h;
        DSW((u16)(fr + 2)) = (u16)(lin - base);
    }
    lib_ems_restore_map(h);
    dos_free(old);
    return bmp;
}

/* AP 1d32:214e bmp_load_ems(x) */
u16 bmp_load_ems(u16 x_ds)
{
    u16 b = bmp_load_planes(x_ds);
    if (b) bmp_to_ems(b);
    return b;
}

/* AP 1d32:2170 bmp_free_ems(bmp): an EMS bitmap gets a dummy DOS block for bmp_free */
void bmp_free_ems(u16 p0_ds)
{
    u16 fr0 = DSW(p0_ds);
    if (DSW(fr0) < 0x50) {
        lib_ems_free(DSW(fr0));
        DSW(fr0) = dos_alloc(16, 0, 0).seg;
    }
    bmp_free(p0_ds);
}

/* The draw wrappers: map the handle, draw with the frame segment patched, restore. */
static u16 ems_patch(u16 fr)
{
    u16 h = DSW(fr);
    if (h < 0x50) DSW(fr) = lib_ems_map(h, 0).seg;
    return h;
}
static void ems_unpatch(u16 fr, u16 h)
{
    DSW(fr) = h;
    if (h < 0x50) lib_ems_restore_map(h);
}

/* AP 1d32:21b6 bmp_draw_ems_flip(frame, x, y, flags) -> gfx_sprite_flip */
void bmp_draw_ems_flip(u16 p0_ds, s16 a1, s16 a2, s16 a3)
{
    u16 h = ems_patch(p0_ds);
    gfx_sprite_flip(p0_ds, a1, a2, a3);
    ems_unpatch(p0_ds, h);
}

/* AP 1d32:2204 bmp_draw_ems_scaled(frame, x, y, flags, w, h) -> gfx_sprite_scaled (no callers) */
void bmp_draw_ems_scaled(u16 p0_ds, s16 a1, s16 a2, s16 a3, s16 a4, s16 a5)
{
    u16 h = ems_patch(p0_ds);
    gfx_sprite_scaled(p0_ds, a1, a2, a3, a4, a5);
    ems_unpatch(p0_ds, h);
}

/* AP 1d32:2258 bmp_draw_ems_rotated(frame, 7 words) -> gfx_sprite_rotated_ind */
void bmp_draw_ems_rotated(u16 a0, u16 a1, u16 a2, u16 a3, u16 a4, u16 a5, u16 a6, u16 a7)
{
    u16 h = ems_patch(a0);
    gfx_sprite_rotated_ind(a0, (s16)a1, (s16)a2, a3, (s16)a4, (s16)a5, (s16)a6, (s16)a7);
    ems_unpatch(a0, h);
}

/* AP 1d32:22b2 emm_pools_clear (no callers) */
void emm_pools_clear(void) { rtl_setmem_ds(POOLS, 4 * POOL_SIZE, 0); }

/* AP 1d32:22c7 emm_first_free(mask): lowest clear slot 1..32, 0 when full */
static s16 emm_first_free(u32 mask)
{
    if (mask == 0xFFFFFFFFu) return 0;
    s16 n = 1;
    for (; mask & 1; mask >>= 1) n++;
    return n;
}

/* AP 1d32:22f4 pool_alloc(p, size) -> p << 8 | slot, 0 on failure */
u16 ap_pool_alloc_1d32(s16 p, u16 size)
{
    if (p == DSS(CUR_POOL)) DSS(CUR_POOL) = -1;
    if (size & 0xF) size = (u16)((size + 15) & 0xFFF0);
    s32 need = (s32)PL(p, P_used) + size;
    s16 k = emm_first_free(PL(p, P_mask));
    if (!k) return 0;
    if ((s32)PL(p, P_cap) < need) {
        if (!PW(p, P_handle)) {
            PW(p, P_handle) = lib_ems_alloc((u32)need);
            if (!PW(p, P_handle)) return 0;
        } else if (!lib_ems_realloc(PW(p, P_handle), (u32)need)) {
            return 0;
        }
        PL(p, P_cap) = (u32)(((need + 0x3FFF) / 0x4000) << 14);
    }
    PW(p, P_off(k)) = (u16)(PL(p, P_used) >> 4);
    PW(p, P_size(k)) = size;
    PL(p, P_mask) |= 1u << (k - 1);
    PL(p, P_used) += size;
    return (u16)(p << 8 | k);
}

/* AP 1d32:23f4 pool_trim: give back the freed tail (only the lowest free slot and those below it) */
static void pool_trim(s16 p)
{
    s16 i = (s16)(emm_first_free(PL(p, P_mask)) - 1);
    u32 bit = 1u << i;
    for (;;) {
        PL(p, P_used) -= PW(p, P_size(i + 1));
        PW(p, P_size(i + 1)) = 0;
        PW(p, P_off(i + 1)) = 0;
        bit >>= 1;
        if (i-- == 0) break;
        if (PL(p, P_mask) & bit) break;
    }
    if (PL(p, P_used) == 0) {
        lib_ems_free(PW(p, P_handle));
        PL(p, P_cap) = 0;
        PW(p, P_handle) = 0;
        return;
    }
    u32 cap = (u32)((((s32)PL(p, P_used) + 0x3FFF) / 0x4000) << 14);
    if (PL(p, P_cap) != cap) {
        lib_ems_realloc(PW(p, P_handle), PL(p, P_used));
        PL(p, P_cap) = cap;
    }
}

/* AP 1d32:24b6 pool_free(id) */
void ap_pool_free_1d32(u16 id)
{
    s16 p = (s16)id >> 8, k = (s16)(id & 0xFF);
    if (p == DSS(CUR_POOL)) DSS(CUR_POOL) = -1;
    PL(p, P_mask) &= ~(1u << (k - 1));
    if (emm_first_free(PL(p, P_mask)) == k) pool_trim(p);
}

/* AP 1d32:2525 pool_map(id): cached mapping (no map save) */
FarPtr pool_map(u16 id)
{
    s16 p = (s16)id >> 8;
    if (p != DSS(CUR_POOL)) {
        DSS(CUR_POOL) = p;
        ds_far_wr(CUR_FRAME, lib_ems_map_nosave(PW(p, P_handle)));
    }
    return far_make((u16)(ds_far(CUR_FRAME).seg + PW(p, P_off(id & 0xFF))), 0);
}

/* AP 1d32:2571 pool_map_save(id): saves the map (pair with pool_unmap) */
FarPtr pool_map_save(u16 id)
{
    s16 p = (s16)id >> 8;
    FarPtr f = lib_ems_map(PW(p, P_handle), 0);
    return far_make((u16)(f.seg + PW(p, P_off(id & 0xFF))), 0);
}

/* AP 1d32:25a5 pool_unmap(id) */
void pool_unmap(u16 id) { lib_ems_restore_map(PW((s16)id >> 8, P_handle)); }
