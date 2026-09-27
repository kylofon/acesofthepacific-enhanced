/* shell_data: sys\farquick.c (PS 1ed0) — far small-block pool allocator behind the shell's
 * farmalloc, shell_data_sys.md §1. Pools: DS:4F6E free counts, DS:4F82 bases, DS:4FAA free heads. */
#include "shell_data/sd_priv.h"

#define POOL_FREE(i) DSW(PS_DS_pool_free + (i) * 2)
#define POOL_BASE(i) (PS_DS_pool_base + (i) * 4)
#define POOL_HEAD(i) (PS_DS_pool_head + (i) * 4)

/* PS 1ed0:0000 pool_create(cell, n) — only called (16, 256) */
FarPtr ps_pool_create(s16 cell, s16 n)
{
    u16 prod = (u16)(cell * n);
    FarPtr p = dos_alloc((u32)prod, 0, prod);
    if (far_is_null(p)) sd_fatal(FS_FARQUICK, 0x15);
    FarPtr c = p;
    for (u16 i = 0; i < (u16)(n - 1); i++) {
        FarPtr nx = far_add(c, (u16)cell);
        fwp(c, 0, nx);
        c = nx;
    }
    fwp(c, 0, far_null());
    return p;
}

/* PS 1ed0:00b2 far_alloc (22ce:022a) */
FarPtr far_alloc(u32 size)
{
    if (size <= 0x10) {
        int i;
        for (i = 0; i < 10 && far_is_null(ds_far(POOL_HEAD(i))); i++) ;
        if (i == 10) {
            for (i = 0; i < 10 && !far_is_null(ds_far(POOL_BASE(i))); i++) ;
            if (i >= 10) sd_fatal(FS_FARQUICK, 0x34);
            FarPtr p = ps_pool_create(16, 256);
            ds_far_wr(POOL_BASE(i), p);
            ds_far_wr(POOL_HEAD(i), p);
            POOL_FREE(i) = 0x100;
        }
        FarPtr p = ds_far(POOL_HEAD(i));
        ds_far_wr(POOL_HEAD(i), fp(p, 0));
        POOL_FREE(i)--;
        return p;
    }
    FarPtr p = dos_alloc(size, 0, 0);
    if (far_is_null(p)) sd_fatal(FS_FARQUICK, 0x3F);
    return p;
}

/* PS 1ed0:01e8 far_free (22ce:02b0) — huge compares (PCMP@/PADD@) against each pool */
void far_free(FarPtr p)
{
    u32 lp = far_lin(p);
    int i;
    for (i = 0; i < 10; i++) {
        FarPtr b = ds_far(POOL_BASE(i));
        if (far_is_null(b)) continue;
        u32 lb = far_lin(b);
        if (lb <= lp && lp < lb + 0x1000) break;
    }
    if (i < 10) {
        fwp(p, 0, ds_far(POOL_HEAD(i)));
        ds_far_wr(POOL_HEAD(i), p);
        if (++POOL_FREE(i) == 0x100) {
            dos_free(ds_far(POOL_BASE(i)));
            ds_far_wr(POOL_BASE(i), far_null());
            ds_far_wr(POOL_HEAD(i), far_null());
            POOL_FREE(i) = 0;
        }
    } else {
        dos_free(p);
    }
}
