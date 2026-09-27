/* render3d — pool.c (AP 3546): fixed-size record pools with free and used lists. render3d_world.md §5.3.
 *
 * Header (14 bytes, near): +00 nfree, +02 nused, +04 size, +06 segment, +08 base offset, +0A free
 * head, +0C used head (node offsets, FFFFh = none). Node = {u16 next, u16 prev} + record; the element
 * pointer is node + 4. */
#include "render3d/r3d_priv.h"
#include "platform/platform.h"

#define POOL_TAG_SEG 0x3B03                     /* "pool.c" tag string for the allocators (3b03:0000) */
static u16 pool_tag(void) { return far_to_near_str(far_make(SEG(POOL_TAG_SEG), 0)); }

/* AP 3546:0118 */
void pool_init(u16 hdr, u16 off, u16 seg, u16 count, u16 size)
{
    DSW(hdr) = count; DSW(hdr + 2) = 0; DSW(hdr + 4) = size; DSW(hdr + 6) = seg;
    DSW(hdr + 8) = off; DSW(hdr + 0x0A) = off; DSW(hdr + 0x0C) = 0xFFFF;
    if (!count) return;
    u16 cx = off, prev = 0xFFFF, last = 0;
    for (s16 i = 0; i < (s16)count; i++) {
        last = cx;
        wr16(seg, cx, (u16)(cx + size + 4));
        wr16(seg, (u16)(cx + 2), prev);
        prev = cx;
        cx = (u16)(cx + size + 4);
    }
    wr16(seg, last, 0xFFFF);
}

/* AP 3546:0006 pool_create — far block (leaks the header when the block fails, as the original) */
u16 ap_pool_create(u16 count, u16 size)
{
    if (size & 1) size++;
    u16 hdr = mem_new(0x0E, 0, pool_tag(), 0x39);
    FarPtr b = mem_far_new((u16)(count * (size + 4)), 0);
    if (!hdr || far_is_null(b)) return 0;
    pool_init(hdr, b.off, b.seg, count, size);
    return hdr;
}
/* AP 3546:007e — near block */
u16 pool_create_near(u16 count, u16 size)
{
    if (size & 1) size++;
    u16 hdr = mem_new(0x0E, 0, pool_tag(), 0x4F);
    if (!hdr) return 0;
    u16 b = mem_new((u16)(count * (size + 4)), 0, pool_tag(), 0x53);
    if (!b) { mem_delete(hdr, 0, pool_tag(), 0x56); return 0; }
    pool_init(hdr, b, cur_dgroup, count, size);
    return hdr;
}
/* AP 3546:019d */
void pool_destroy(u16 hdr)
{
    mem_far_delete(far_make(DSW(hdr + 6), DSW(hdr + 8)));
    mem_delete(hdr, 0, pool_tag(), 0x87);
}
/* AP 3546:01d4 */
void pool_destroy_near(u16 hdr)
{
    mem_delete(DSW(hdr + 8), 0, pool_tag(), 0x8E);
    mem_delete(hdr, 0, pool_tag(), 0x8F);
}

/* AP 3546:021a pool_alloc — the free head moves to the head of the used list */
u32 ap_pool_alloc_3546(u16 hdr)
{
    if (!DSW(hdr)) return 0;
    u16 seg = DSW(hdr + 6), node = DSW(hdr + 0x0A);
    u32 r = (u32)seg << 16 | (u16)(node + 4);
    DSW(hdr)--; DSW(hdr + 2)++;
    DSW(hdr + 0x0A) = rd16(seg, node);
    if (DSW(hdr + 0x0A) != 0xFFFF) wr16(seg, (u16)(DSW(hdr + 0x0A) + 2), 0xFFFF);
    if (DSW(hdr + 0x0C) != 0xFFFF) {
        wr16(seg, node, DSW(hdr + 0x0C));
        wr16(seg, (u16)(DSW(hdr + 0x0C) + 2), node);
    } else wr16(seg, node, 0xFFFF);
    DSW(hdr + 0x0C) = node;
    wr16(seg, (u16)(node + 2), 0xFFFF);
    return r;
}
/* AP 3546:02bb — wrapper (no callers) */
u32 pool_alloc2(u16 hdr) { return ap_pool_alloc_3546(hdr); }
/* AP 3546:02ca — allocate and zero `size` bytes */
u32 pool_alloc_zero(u16 hdr)
{
    u32 p = ap_pool_alloc_3546(hdr);
    if (p) huge_memset_fp(far_make((u16)(p >> 16), (u16)p), 0, (u32)(s32)(s16)DSW(hdr + 4));
    return p;
}
/* AP 3546:0304 — near pool: zero `size` bytes at DS:p */
u16 pool_alloc_zero_near(u16 hdr)
{
    u16 p = (u16)ap_pool_alloc_3546(hdr);
    if (p) memset(DSP(p), 0, DSW(hdr + 4));
    return p;
}

/* AP 3546:0333 pool_free — (off, seg) normalised to the pool's segment */
void ap_pool_free_3546(u16 hdr, u16 off, u16 seg)
{
    DSW(hdr + 2)--; DSW(hdr)++;
    u16 ps = DSW(hdr + 6);
    if (seg != ps) off = (u16)(off + ((u16)(seg - ps) << 4));
    u16 node = (u16)(off - 4);
    u16 next = rd16(ps, node), prev = rd16(ps, (u16)(node + 2));
    if (prev != 0xFFFF) wr16(ps, prev, next);
    else DSW(hdr + 0x0C) = next;
    next = rd16(ps, node);
    if (next != 0xFFFF) wr16(ps, (u16)(next + 2), rd16(ps, (u16)(node + 2)));
    wr16(ps, node, DSW(hdr + 0x0A));
    DSW(hdr + 0x0A) = node;
    if (rd16(ps, node) != 0xFFFF) wr16(ps, (u16)(rd16(ps, node) + 2), node);
    wr16(ps, (u16)(node + 2), 0xFFFF);
}
/* AP 3546:03f2 */
void pool_free2(u16 hdr, u16 p) { ap_pool_free_3546(hdr, p, DSW(hdr + 6)); }

/* AP 3546:040a — first (p = 0) or next used element, most recent first */
u32 pool_next(u16 hdr, FarPtr p)
{
    u16 seg = DSW(hdr + 6);
    if (far_is_null(p)) {
        u16 u = DSW(hdr + 0x0C);
        return u == 0xFFFF ? 0 : ((u32)seg << 16 | (u16)(u + 4));
    }
    u16 nx = rd16(p.seg, (u16)(p.off - 4));
    return nx == 0xFFFF ? 0 : ((u32)seg << 16 | (u16)(nx + 4));
}
/* AP 3546:045f */
u16 pool_next_near(u16 hdr, u16 p)
{
    if (!p) { u16 u = DSW(hdr + 0x0C); return u == 0xFFFF ? 0 : (u16)(u + 4); }
    u16 nx = DSW(p - 4);
    return nx == 0xFFFF ? 0 : (u16)(nx + 4);
}
/* AP 3546:0498 — free count */
u16 pool_field0(u16 hdr) { return DSW(hdr); }
/* AP 3546:04a4 — used count */
u16 pool_misc(u16 hdr) { return DSW(hdr + 2); }
