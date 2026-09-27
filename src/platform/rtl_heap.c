/* Borland C++ 3.0 RTL near heap (medium model), block for block as the original so that near
 * addresses match: the heap grows from the end of BSS (__brklvl, DS:009C) towards the stack.
 * Block header: +0 size (even, bit 0 = in use, header included), +2 previous physical block; free
 * blocks: +4 next free, +6 previous free (circular list through __rover). Minimum block 8 bytes.
 * The stack limit of __brk/__sbrk is the emulated SP (dg_sp) - 200h. Addresses INTRO / PS / AP. */
#include "platform/rtl_priv.h"

#define HSZ(b)   DSW(b)
#define HPREV(b) DSW((u16)((b) + 2))
#define HNEXTF(b) DSW((u16)((b) + 4))
#define HPREVF(b) DSW((u16)((b) + 6))

/* INTRO 0000:1813 / PS 0000:2503 / AP 0000:286f __brk */
s16 rtl___brk(u16 addr)
{
    if (addr < (u16)(dg_sp - 0x200)) {
        DSW(RTL_DS_brklvl) = addr;
        return 0;
    }
    DSW(RTL_DS_errno) = 8;                           /* ENOMEM */
    return -1;
}

/* INTRO 0000:1835 / PS 0000:2525 / AP 0000:2891 __sbrk(long incr): returns the old break or FFFFh */
u16 rtl___sbrk(u32 incr)
{
    u32 t = (u32)(u16)incr + DSW(RTL_DS_brklvl) + ((incr >> 16) << 16);
    u16 ax = (u16)t;
    if ((t >> 16) == 0) {
        u32 cx = (u32)ax + 0x200;
        if (cx <= 0xFFFF && (u16)cx < dg_sp) {
            u16 old = DSW(RTL_DS_brklvl);
            DSW(RTL_DS_brklvl) = ax;
            return old;
        }
    }
    DSW(RTL_DS_errno) = 8;
    return 0xFFFF;
}

/* INTRO 0000:1a5f / PS 0000:274f / AP 0000:301c: unlink a free block */
static void free_unlink(u16 bx)
{
    u16 di = HPREVF(bx);
    if (bx == di) { DSW(RL(rover)) = 0; return; }
    DSW(RL(rover)) = di;
    u16 si = HNEXTF(bx);
    HNEXTF(di) = si;
    HPREVF(si) = di;
}

/* INTRO 0000:1a7b / PS 0000:276b / AP 0000:3038: insert a free block before the rover */
static void free_insert(u16 bx)
{
    u16 si = DSW(RL(rover));
    if (si) {
        u16 di = HPREVF(si);
        HPREVF(si) = bx;
        HNEXTF(di) = bx;
        HPREVF(bx) = di;
        HNEXTF(bx) = si;
    } else {
        DSW(RL(rover)) = bx;
        HNEXTF(bx) = bx;
        HPREVF(bx) = bx;
    }
}

/* INTRO 0000:19ec / PS 0000:26dc / AP 0000:2fa9: free the last block (lowers the break) */
static void free_last(u16 bx)
{
    if (DSW(RL(first)) != bx) {
        u16 si = HPREV(bx);
        if (HSZ(si) & 1) {
            DSW(RL(last)) = si;
        } else if (si != DSW(RL(first))) {
            bx = si;
            free_unlink(bx);
            DSW(RL(last)) = HPREV(bx);
        } else {
            bx = si;
            goto empty;
        }
        rtl___brk(bx);
        return;
    }
empty:
    DSW(RL(first)) = 0;
    DSW(RL(last)) = 0;
    DSW(RL(rover)) = 0;
    rtl___brk(bx);
}

/* INTRO 0000:1a26 / PS 0000:2716 / AP 0000:2fe3: free an inner block, merging with its neighbours */
static void free_inner(u16 bx)
{
    HSZ(bx) = (u16)(HSZ(bx) - 1);
    bool merged = false;
    if (bx != DSW(RL(first))) {
        u16 si = HPREV(bx);
        u16 ax = HSZ(si);
        if (!(ax & 1)) {                             /* previous block free: merge into it */
            ax = (u16)(ax + HSZ(bx));
            HSZ(si) = ax;
            u16 di = (u16)(bx + HSZ(bx));
            HPREV(di) = si;
            bx = si;
            merged = true;
        }
    }
    if (!merged) free_insert(bx);
    u16 di = (u16)(bx + HSZ(bx));
    u16 ax = HSZ(di);
    if (ax & 1) return;                              /* next block in use */
    HSZ(bx) = (u16)(HSZ(bx) + ax);                   /* next block free: absorb it */
    u16 si = (u16)(di + ax);
    HPREV(si) = bx;
    free_unlink(di);
}

/* INTRO 0000:19cf / PS 0000:26bf / AP 0000:2f8c free */
void rtl_free(u16 block_ds)
{
    if (block_ds < 4) return;
    u16 bx = (u16)(block_ds - 4);
    if (bx == DSW(RL(last))) free_last(bx);
    else free_inner(bx);
}

/* INTRO 0000:1afe / PS 0000:27ee / AP 0000:30bb: first allocation (aligns the break) */
static u16 alloc_first(u16 n)
{
    u16 ax = rtl___sbrk(0);
    if (ax & 1) rtl___sbrk(1);
    ax = rtl___sbrk(n);
    if (ax == 0xFFFF) return 0;
    DSW(RL(first)) = ax;
    DSW(RL(last)) = ax;
    HSZ(ax) = (u16)(n + 1);
    return (u16)(ax + 4);
}

/* INTRO 0000:1b3e / PS 0000:282e / AP 0000:30fb: grow the heap by a new last block */
static u16 alloc_extend(u16 n)
{
    u16 bx = rtl___sbrk(n);
    if (bx == 0xFFFF) return 0;
    HPREV(bx) = DSW(RL(last));
    DSW(RL(last)) = bx;
    HSZ(bx) = (u16)(n + 1);
    return (u16)(bx + 4);
}

/* INTRO 0000:1b67 / PS 0000:2857 / AP 0000:3124: split a free block, the new block at its end */
static u16 alloc_split(u16 bx, u16 n)
{
    HSZ(bx) = (u16)(HSZ(bx) - n);
    u16 si = (u16)(bx + HSZ(bx));
    u16 di = (u16)(si + n);
    HSZ(si) = (u16)(n + 1);
    HPREV(si) = bx;
    HPREV(di) = si;
    return (u16)(si + 4);
}

/* INTRO 0000:1a9e / PS 0000:278e / AP 0000:305b malloc */
u16 rtl_malloc(u16 size)
{
    if (size == 0) return 0;
    u32 t = (u32)size + 5;
    if (t > 0xFFFF) return 0;
    u16 n = (u16)(t & 0xFFFE);
    if (n < 8) n = 8;
    if (DSW(RL(first)) == 0) return alloc_first(n);
    u16 bx = DSW(RL(rover));
    if (bx) {
        u16 dx = bx;
        do {
            if (HSZ(bx) >= n) {
                if (HSZ(bx) >= (u16)(n + 8)) return alloc_split(bx, n);
                free_unlink(bx);
                HSZ(bx) = (u16)(HSZ(bx) + 1);
                return (u16)(bx + 4);
            }
            bx = HPREVF(bx);
        } while (bx != dx);
    }
    return alloc_extend(n);
}

/* INTRO 0000:1882 / PS 0000:2572 / AP 0000:28de calloc */
u16 rtl_calloc(u16 n, u16 size)
{
    u32 total = (u32)n * size;
    if (total > 0xFFFF) return 0;
    u16 p = rtl_malloc((u16)total);
    if (p) rtl_setmem_ds(p, (u16)total, 0);
    return p;
}

/* INTRO 0000:1bf9 / PS 0000:28e9 / AP 0000:31b6 realloc */
u16 rtl_realloc(u16 block_ds, u16 size)
{
    if (size == 0) { rtl_free(block_ds); return 0; }
    if (block_ds == 0) return rtl_malloc(size);
    u16 bx = (u16)(block_ds - 4);
    u16 cx = (u16)(HSZ(bx) - 1);
    u16 dx = (u16)((size + 5) & 0xFFFE);
    if (dx < 8) dx = 8;
    if (cx == dx) return block_ds;
    if (cx < dx) {                                   /* INTRO 0000:1b80 ...: move */
        u16 np = rtl_malloc(size);
        if (!np) return 0;
        u16 words = (u16)((HSZ(bx) - 5) >> 1);
        for (u16 i = 0; i < words; i++) DSW((u16)(np + 2 * i)) = DSW((u16)(block_ds + 2 * i));
        rtl_free(block_ds);
        return np;
    }
    /* INTRO 0000:1bb5 ...: shrink in place */
    u16 ax = dx;
    if ((u16)(dx + 8) > cx) return block_ds;
    dx = cx;
    if (bx == DSW(RL(last))) {
        HSZ(bx) = (u16)(ax + 1);
        rtl___brk((u16)(ax + bx + 1 - 1));
        return block_ds;
    }
    u16 di = (u16)(bx + ax);
    HPREV(di) = bx;
    dx = (u16)(dx - ax);
    HSZ(bx) = (u16)(HSZ(bx) - dx);
    u16 si = (u16)(di + dx);
    HPREV(si) = di;
    HSZ(di) = (u16)(dx + 1);
    free_inner(di);
    return block_ds;
}

/* INTRO 0000:1866 / PS 0000:2556 / AP 0000:28c2 brk, 0000:1872 / 2562 / 28ce sbrk (not called by the
 * game; kept for completeness) */
s16 rtl_brk(u16 addr) { return rtl___brk(addr); }
u16 rtl_sbrk(s16 incr) { return rtl___sbrk((u32)(s32)incr); }
